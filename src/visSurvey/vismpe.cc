/*+
 * (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 * AUTHOR   : K. Tingdahl
 * DATE     : Oct 1999
-*/

static const char* rcsID mUsedVar = "$Id$";

#include "vismpe.h"

#include "visboxdragger.h"
#include "visdepthtabplanedragger.h"
#include "visevent.h"
#include "vismaterial.h"
#include "vistransform.h"
#include "visvolorthoslice.h"
#include "visselman.h"
#include "vistexturechannels.h"
#include "vistexturechannel2rgba.h"

#include "arrayndsubsel.h"
#include "attribdatapack.h"
#include "keystrs.h"
#include "mpeengine.h"
#include "settings.h"
#include "survinfo.h"
#include "zaxistransform.h"
#include "zaxistransformer.h"

#include "arrayndimpl.h"

namespace visSurvey {

MPEDisplay::MPEDisplay()
    : VisualObjectImpl(true)
    , boxdragger_(visBase::BoxDragger::create())
    , issliceshown_(false)
    , allowshading_(false)
    , datatransform_(0)
    , cacheid_(DataPack::cNoID())
    , volumecache_(0)
    , channels_(visBase::TextureChannels::create())
    , dim_(0)
    , engine_(MPE::engine())
    , sceneeventcatcher_(0)
    , as_(*new Attrib::SelSpec())
    , manipulated_(false)
    , movement( this )
    , boxDraggerStatusChange( this )
    , planeOrientationChange( this )
    , curtexturecs_(false)
    , curtextureas_(*new Attrib::SelSpec())
{
    boxdragger_->ref();
    boxdragger_->finished.notify( mCB(this,MPEDisplay,boxDraggerFinishCB) );
    boxdragger_->setBoxTransparency( 0.7 );
    addChild( boxdragger_->osgNode() );
    showBoxDragger( false );

    updateBoxSpace();

    channels_->ref();
    channels_->setChannels2RGBA( visBase::ColTabTextureChannel2RGBA::create() );

    addSlice( cInLine(), false );

    engine_.activevolumechange.notify( mCB(this,MPEDisplay,updateBoxPosition) );

    updateBoxPosition( 0 );

    turnOn( true );

    int buttonkey = OD::NoButton;
    mSettUse( get, "dTect.MouseInteraction", sKeyBoxDepthKey(), buttonkey );
    boxdragger_->setPlaneTransDragKeys( true, buttonkey );
    buttonkey = OD::ShiftButton;
    mSettUse( get, "dTect.MouseInteraction", sKeyBoxPlaneKey(), buttonkey );
    boxdragger_->setPlaneTransDragKeys( false, buttonkey );

    bool useindepthtransforresize = true;
    mSettUse( getYN, "dTect.MouseInteraction", sKeyInDepthBoxResize(),
	      useindepthtransforresize );
    boxdragger_->useInDepthTranslationForResize( useindepthtransforresize );
}


MPEDisplay::~MPEDisplay()
{
    engine_.activevolumechange.remove( mCB(this,MPEDisplay,updateBoxPosition) );

    setSceneEventCatcher( 0 );

    DPM( DataPackMgr::CubeID() ).release( cacheid_ );
    if ( volumecache_ )
	DPM( DataPackMgr::CubeID() ).release( volumecache_ );

    TypeSet<int> children;
    getChildren( children );
    for ( int idx=0; idx<children.size(); idx++ )
	removeChild( children[idx] );

    channels_->unRef();

    setZAxisTransform( 0, 0 );

    boxdragger_->finished.remove( mCB(this,MPEDisplay,boxDraggerFinishCB) );
    boxdragger_->unRef();

    delete &as_;
    delete &curtextureas_;
}


void MPEDisplay::setColTabMapperSetup( int attrib,
				       const ColTab::MapperSetup& ms,
				       TaskRunner* tr )
{
    if ( attrib<0 || attrib>=nrAttribs() )
	return;

    channels_->setColTabMapperSetup( attrib, ms );
    channels_->reMapData( attrib, false, 0 );
}


void MPEDisplay::setColTabSequence( int attrib, const ColTab::Sequence& seq,
				    TaskRunner* tr )
{
    if ( attrib>=0 && attrib<nrAttribs() && channels_->getChannels2RGBA() )
	channels_->getChannels2RGBA()->setSequence( attrib, seq );
}


const ColTab::MapperSetup* MPEDisplay::getColTabMapperSetup( int attrib,
	int version ) const
{
    if ( attrib<0 || attrib>=nrAttribs() )
	return 0;

    if ( mIsUdf(version) || version<0
			 || version >= channels_->nrVersions(attrib) )
	version = channels_->currentVersion( attrib );

    return &channels_->getColTabMapperSetup( attrib, version );
}


const ColTab::Sequence* MPEDisplay::getColTabSequence( int attrib ) const
{
    return ( attrib>=0 && attrib<nrAttribs() && channels_->getChannels2RGBA() )
	? channels_->getChannels2RGBA()->getSequence( attrib ) : 0;
}


bool MPEDisplay::canSetColTabSequence() const
{
    return ( channels_->getChannels2RGBA() ) ?
	channels_->getChannels2RGBA()->canSetSequence() : false;
}


CubeSampling MPEDisplay::getCubeSampling( int attrib ) const
{
    return getCubeSampling( true, false, attrib );
}


CubeSampling MPEDisplay::getBoxPosition() const
{
    Coord3 center = boxdragger_->center();
    Coord3 width = boxdragger_->width();

    CubeSampling cube;
    cube.hrg.start = BinID( mNINT32(center.x-width.x/2),
			    mNINT32(center.y-width.y/2) );
    cube.hrg.stop = BinID( mNINT32(center.x+width.x/2),
			   mNINT32(center.y+width.y/2) );
    cube.hrg.step = BinID( SI().inlStep(), SI().crlStep() );
    cube.zrg.start = (float) ( center.z - width.z / 2 );
    cube.zrg.stop = (float) ( center.z + width.z / 2 );
    cube.zrg.step = SI().zStep();
    cube.hrg.snapToSurvey();
    SI().snapZ( cube.zrg.start, 0 );
    SI().snapZ( cube.zrg.stop, 0 );
    return cube;
}


bool MPEDisplay::getPlanePosition( CubeSampling& planebox ) const
{
    if ( slices_.isEmpty() )
	return false;

    const visBase::DepthTabPlaneDragger* drg = slices_[0]->getDragger();
    const int dim = dim_;

    Coord3 center = drg->center();

    Interval<float> sx, sy, sz;
    drg->getSpaceLimits( sx, sy, sz );

    if ( !dim )
    {
	planebox.hrg.start.inl() = SI().inlRange(true).snap( center.x );
	planebox.hrg.stop.inl() = planebox.hrg.start.inl();

	planebox.hrg.start.crl() = SI().crlRange(true).snap( sy.start );
	planebox.hrg.stop.crl() =  SI().crlRange(true).snap( sy.stop );

	planebox.zrg.start = SI().zRange(true).snap( sz.start );
	planebox.zrg.stop = SI().zRange(true).snap( sz.stop );
    }
    else if ( dim==1 )
    {
	planebox.hrg.start.inl() = SI().inlRange(true).snap( sx.start );
	planebox.hrg.stop.inl() =  SI().inlRange(true).snap( sx.stop );

	planebox.hrg.stop.crl() = SI().crlRange(true).snap( center.y );
	planebox.hrg.start.crl() = planebox.hrg.stop.crl();

	planebox.zrg.start = SI().zRange(true).snap( sz.start );
	planebox.zrg.stop = SI().zRange(true).snap( sz.stop );
    }
    else
    {
	planebox.hrg.start.inl() = SI().inlRange(true).snap( sx.start );
	planebox.hrg.stop.inl() =  SI().inlRange(true).snap( sx.stop );

	planebox.hrg.start.crl() = SI().crlRange(true).snap( sy.start );
	planebox.hrg.stop.crl() =  SI().crlRange(true).snap( sy.stop );

	planebox.zrg.stop = SI().zRange(true).snap( center.z );
	planebox.zrg.start = planebox.zrg.stop;
    }

    planebox.hrg.step = BinID( SI().inlStep(), SI().crlStep() );
    planebox.zrg.step = SI().zRange(true).step;

    return true;
}


void MPEDisplay::setSelSpec( int attrib, const Attrib::SelSpec& as )
{
    if ( attrib  || as_ == as )
	return;

    as_ = as;

    // empty the cache first
    if ( volumecache_ )
    {
	DPM(DataPackMgr::CubeID()).release( volumecache_ );
	volumecache_ = 0;
    }

    channels_->setUnMappedData( attrib, 0, 0, OD::UsePtr, 0 );

    const char* usrref = as.userRef();
    BufferStringSet* attrnms = new BufferStringSet();
    attrnms->add( usrref );
    if ( userrefs_.isEmpty() )
	userrefs_ += attrnms;
    else
    delete userrefs_.replace( attrib, attrnms );

    if ( ( !usrref || !*usrref ) && channels_->getChannels2RGBA() )
	channels_->getChannels2RGBA()->setEnabled( attrib, false );
}


const Attrib::SelSpec* MPEDisplay::getSelSpec( int attrib ) const
{
    return attrib ? 0 : &as_;
}


const char* MPEDisplay::getSelSpecUserRef() const
{
    if ( as_.id().asInt()==Attrib::SelSpec::cNoAttrib().asInt() )
	return sKey::None();
    else if ( as_.id().asInt()==Attrib::SelSpec::cAttribNotSel().asInt() )
	return 0;

    return as_.userRef();
}


NotifierAccess* MPEDisplay::getMovementNotifier()
{
    return &movement;
}


NotifierAccess* MPEDisplay::getManipulationNotifier()
{
    return &movement;
}


void MPEDisplay::moveMPEPlane( int nr )
{
    if ( slices_.isEmpty() )
	return;

    visBase::DepthTabPlaneDragger* drg = slices_[0]->getDragger();
    if ( !drg || !nr ) return;
    const int dim = dim_;

    Coord3 center = drg->center();
    Coord3 width = boxdragger_->width();

    Interval<float> sx, sy, sz;
    drg->getSpaceLimits( sx, sy, sz );

    center.x = 0.5 * ( SI().inlRange(true).snap( center.x - width.x/2 ) +
		       SI().inlRange(true).snap( center.x + width.x/2 ) );
    center.y = 0.5 * ( SI().crlRange(true).snap( center.y - width.y/2 ) +
		       SI().crlRange(true).snap( center.y + width.y/2 ) );
    center.z = 0.5 * ( SI().zRange(true).snap( center.z - width.z/2 ) +
		       SI().zRange(true).snap( center.z + width.z/2 ) );

    const int nrsteps = abs(nr);
    const float sign = nr > 0 ? 1.001f : -1.001f;
    // sign is slightly to big to avoid that it does not trigger a track

    sx.widen( 0.5f*SI().inlStep(), true ); sx.sort();
    sy.widen( 0.5f*SI().crlStep(), true ); sy.sort();
    sz.widen( 0.5f*SI().zStep(), true ); sz.sort();
    // assure that border lines of survey are reachable in spite of foregoing

    for ( int idx=0; idx<nrsteps; idx++ )
    {
	if ( !dim )
	    center.x += sign * SI().inlStep();
	else if ( dim==1 )
	    center.y += sign * SI().crlStep();
	else
	    center.z += sign * SI().zStep();

	if ( !sx.includes(center.x,false) || !sy.includes(center.y,false) ||
	     !sz.includes(center.z,false) )
	    return;

	slices_[0]->setCenter( center, false );
    }

    movement.trigger();
}


void MPEDisplay::setSceneEventCatcher( visBase::EventCatcher* nevc )
{
    if ( sceneeventcatcher_ )
    {
	sceneeventcatcher_->eventhappened.remove(
					mCB(this,MPEDisplay,mouseClickCB) );
	sceneeventcatcher_->unRef();
    }

    sceneeventcatcher_ = nevc;

    if ( sceneeventcatcher_ )
    {
	sceneeventcatcher_->ref();
	sceneeventcatcher_->eventhappened.notify(
	    mCB(this,MPEDisplay,mouseClickCB) );
    }
}


// Binary compatibility: updated and next called for one MPEDisplay at a time
static MouseCursor mousecursor_;

const MouseCursor* MPEDisplay::getMouseCursorNonVirtual() const
{
    return &mousecursor_;
}


void MPEDisplay::updateMouseCursorCB( CallBacker* cb )
{
    if ( !isOn() || isLocked() )
	mousecursor_.shape_ = MouseCursor::NotSet;
    else
    {
	initAdaptiveMouseCursor( cb, id(),
		boxdragger_->getPlaneTransDragKeys(false), mousecursor_ );

	 if ( cb )
	 {
	     // Check for tracker plane
	     mCBCapsuleUnpack(const visBase::EventInfo&,eventinfo,cb);
	     if ( !slices_.isEmpty() &&
		  eventinfo.pickedobjids.isPresent(slices_[0]->id()) )
	     {
		 if ( !slices_[0]->isPickingEnabled() )
		     mousecursor_.shape_ = MouseCursor::PointingHand;
		 else
		     mousecursor_.shape_ = MouseCursor::NotSet;
	     }
	 }
    }
}


void MPEDisplay::boxDraggerFinishCB(CallBacker*)
{
    manipulated_ = true;
}


void MPEDisplay::showBoxDragger( bool yn )
{
    if ( yn==boxdragger_->isOn() )
	return;

    boxdragger_->turnOn( yn );
    boxDraggerStatusChange.trigger();
}


void MPEDisplay::updateSeedOnlyPropagation( bool yn )
{
    engine_.updateSeedOnlyPropagation( yn );
}


void MPEDisplay::updateMPEActiveVolume()
{
    if ( manipulated_ )
    {
	const CubeSampling newcube = getBoxPosition();
	engine_.setActiveVolume( newcube );
	manipulated_ = false;
    }
}


void MPEDisplay::removeSelectionInPolygon( const Selector<Coord3>& selector,
	TaskRunner* tr )
{
    engine_.removeSelectionInPolygon( selector, tr );
    manipulated_ = true;
}


bool MPEDisplay::isOn() const
{
    return visBase::VisualObjectImpl::isOn() &&
	( isBoxDraggerShown() || isDraggerShown() );
}


bool MPEDisplay::isBoxDraggerShown() const
{ return boxdragger_->isOn(); }


void MPEDisplay::setDraggerTransparency( float transparency )
{
    for ( int idx=0; idx<slices_.size(); idx++ )
	if ( slices_[idx]->getMaterial() )
	    slices_[idx]->getMaterial()->setTransparency( transparency );
}


float MPEDisplay::getDraggerTransparency() const
{
    return !slices_.isEmpty() && slices_[0]->getMaterial()
			? slices_[0]->getMaterial()->getTransparency() : 0;
}


void MPEDisplay::showDragger( bool yn )
{
    if ( yn==isDraggerShown() )
	return;

    issliceshown_ = yn;

    if ( yn )
	updateSlice();
    else
	turnOnSlice( false );

    movement.trigger();
    planeOrientationChange.trigger();
}


bool MPEDisplay::isDraggerShown() const
{
    return issliceshown_;
}


void MPEDisplay::setPlaneOrientation( int orient )
{
    if ( orient<0 || orient>2 )
	return;

    dim_ = orient;

    setSliceDimension( 0, dim_ );

    if ( !isOn() ) return;

    movement.trigger();
}


int MPEDisplay::getPlaneOrientation() const
{
    return dim_;
}


void MPEDisplay::mouseClickCB( CallBacker* cb )
{
    if ( sceneeventcatcher_->isHandled() || !isOn() ) return;

    mCBCapsuleUnpack(const visBase::EventInfo&,eventinfo,cb);
    if ( eventinfo.type != visBase::MouseClick )
	return;

    if ( OD::leftMouseButton(eventinfo.buttonstate_) &&
	 OD::shiftKeyboardButton(eventinfo.buttonstate_) &&
	 !OD::ctrlKeyboardButton(eventinfo.buttonstate_) &&
	 !OD::altKeyboardButton(eventinfo.buttonstate_) &&
	 eventinfo.pickedobjids.isPresent(id()) )
    {
	if ( eventinfo.pressed )
	{
	    if ( ++dim_>=3 )
		dim_ = 0;

	    setSliceDimension( 0, dim_ );
	    MPE::TrackPlane ntp = engine_.trackPlane();
	    getPlanePosition( ntp.boundingBox() );
	    engine_.setTrackPlane( ntp, false );
	    updateRanges( true, true );   // to do: check
	    movement.trigger();
	    planeOrientationChange.trigger();
	}
	sceneeventcatcher_->setHandled();
    }
    else if ( OD::rightMouseButton( eventinfo.buttonstate_ ) &&
	      OD::shiftKeyboardButton(eventinfo.buttonstate_) &&
	      !OD::ctrlKeyboardButton(eventinfo.buttonstate_) &&
	      !OD::altKeyboardButton(eventinfo.buttonstate_) &&
	      eventinfo.pickedobjids.isPresent(id()) && isDraggerShown() )
    {
	if ( eventinfo.pressed )
	{
	    const MPE::TrackPlane::TrackMode tm =
					engine_.trackPlane().getTrackMode();
	    if ( tm==MPE::TrackPlane::Move )
		engine_.setTrackMode( MPE::TrackPlane::Extend );
	    else if ( tm==MPE::TrackPlane::Extend )
		engine_.setTrackMode( MPE::TrackPlane::ReTrack );
	    else if ( tm==MPE::TrackPlane::ReTrack )
		engine_.setTrackMode( MPE::TrackPlane::Erase );
	    else
		engine_.setTrackMode( MPE::TrackPlane::Move );
	}
	sceneeventcatcher_->setHandled();
    }
    else if ( OD::leftMouseButton(eventinfo.buttonstate_) &&
	      !OD::shiftKeyboardButton(eventinfo.buttonstate_) &&
	      !OD::ctrlKeyboardButton(eventinfo.buttonstate_) &&
	      !OD::altKeyboardButton(eventinfo.buttonstate_) &&
	      !eventinfo.pickedobjids.isPresent(boxdragger_->id()) &&
	      isBoxDraggerShown() && isPickable() )
    {
	sceneeventcatcher_->setHandled();
	if ( !eventinfo.pressed )
	    showBoxDragger( false );
    }
}


void MPEDisplay::freezeBoxPosition( bool yn )
{
    if ( yn )
    {
	engine_.activevolumechange.remove(
				   mCB(this,MPEDisplay,updateBoxPosition) );
    }
    else
    {
	engine_.activevolumechange.notifyIfNotNotified(
				   mCB(this,MPEDisplay,updateBoxPosition) );
    }
}


void MPEDisplay::updateBoxPosition( CallBacker* )
{
    CubeSampling cube = engine_.activeVolume();
    Coord3 newwidth( cube.hrg.stop.inl()-cube.hrg.start.inl(),
		     cube.hrg.stop.crl()-cube.hrg.start.crl(),
		     cube.zrg.stop-cube.zrg.start );

    // Workaround for deadlock in COIN's polar_decomp() or Math::Sqrt(), which
    // occasionally occurs in case the box has one side of zero length.
    if ( cube.hrg.nrInl()==1 )
	newwidth.x = 0.1 * cube.hrg.step.inl();
    if ( cube.hrg.nrCrl()==1 )
	newwidth.y = 0.1 * cube.hrg.step.crl();
	if ( cube.zrg.nrSteps()==0 )
	newwidth.z = 0.1 * cube.zrg.step;

    boxdragger_->setWidth( newwidth );

    const Coord3 newcenter( 0.5*(cube.hrg.stop.inl()+cube.hrg.start.inl()),
			    0.5*(cube.hrg.stop.crl()+cube.hrg.start.crl()),
			    cube.zrg.center());

    boxdragger_->setCenter( newcenter );

    if ( isDraggerShown() )
	updateSlice();

    movement.trigger();
    planeOrientationChange.trigger();
}


void MPEDisplay::updateBoxSpace()
{
    const HorSampling& hs = SI().sampling(true).hrg;
    const Interval<float> survinlrg( mCast(float,hs.start.inl()),
					mCast(float,hs.stop.inl()) );
    const Interval<float> survcrlrg( mCast(float,hs.start.crl()),
					mCast(float,hs.stop.crl()) );
    const Interval<float> survzrg( SI().zRange(true).start,
				   SI().zRange(true).stop );

    boxdragger_->setSpaceLimits( survinlrg, survcrlrg, survzrg );

    const int minwidth = 1;
    boxdragger_->setWidthLimits(
	Interval<float>( float(minwidth*hs.step.inl()), mUdf(float) ),
	Interval<float>( float(minwidth*hs.step.crl()), mUdf(float) ),
	Interval<float>( minwidth*SI().zRange(true).step, mUdf(float) ) );
}


float MPEDisplay::calcDist( const Coord3& pos ) const
{
    const mVisTrans* utm2display = scene_->getUTM2DisplayTransform();
    Coord3 xytpos;
    utm2display->transformBack( pos, xytpos );
    const BinID binid = SI().transform( Coord(xytpos.x,xytpos.y) );

    CubeSampling cs;
    if ( !getPlanePosition(cs) )
	return mUdf(float);

    BinID inlcrldist( 0, 0 );
    float zdiff = 0;

    inlcrldist.inl() =
	binid.inl()>=cs.hrg.start.inl() && binid.inl()<=cs.hrg.stop.inl()
	     ? 0
	     : mMIN( abs(binid.inl()-cs.hrg.start.inl()),
		     abs( binid.inl()-cs.hrg.stop.inl()) );
    inlcrldist.crl() =
        binid.crl()>=cs.hrg.start.crl() && binid.crl()<=cs.hrg.stop.crl()
             ? 0
	     : mMIN( abs(binid.crl()-cs.hrg.start.crl()),
		     abs( binid.crl()-cs.hrg.stop.crl()) );
    const float zfactor = scene_ ? scene_->getZScale() : SI().zScale();
    zdiff = (float) ( cs.zrg.includes(xytpos.z,false)
	     ? 0
	     : mMIN(xytpos.z-cs.zrg.start,xytpos.z-cs.zrg.stop) *
	       zfactor  * scene_->getFixedZStretch() );

    const float inldist = SI().inlDistance();
    const float crldist = SI().crlDistance();
    float inldiff = inlcrldist.inl() * inldist;
    float crldiff = inlcrldist.crl() * crldist;

    return Math::Sqrt( inldiff*inldiff + crldiff*crldiff + zdiff*zdiff );
}


float MPEDisplay::maxDist() const
{
    const float zfactor = scene_ ? scene_->getZScale() : SI().zScale();
    float maxzdist = zfactor * scene_->getFixedZStretch() * SI().zStep() / 2;
    return engine_.trackPlane().boundingBox().nrZ()==1
				? maxzdist : SurveyObject::sDefMaxDist();
}


void MPEDisplay::getMousePosInfo( const visBase::EventInfo&, Coord3& pos,
				  BufferString& val, BufferString& info ) const
{
    val = "undef";
    info = "";

    if ( !isBoxDraggerShown() )
        val = getValue( pos );
}


void MPEDisplay::fillPar( IOPar& par ) const
{
    visBase::VisualObjectImpl::fillPar( par );
    visSurvey::SurveyObject::fillPar( par );

    as_.fillPar( par );
    par.set( sKeyTransparency(), getDraggerTransparency() );
    par.setYN( sKeyBoxShown(), isBoxDraggerShown() );
}


bool MPEDisplay::usePar( const IOPar& par )
{
    if ( !visBase::VisualObjectImpl::usePar( par ) ||
	 !visSurvey::SurveyObject::usePar( par ) )
	 return false;

    float transparency = 0.5;
    par.get( sKeyTransparency(), transparency );
    setDraggerTransparency( transparency );

    bool dispboxdragger = false;
    par.getYN( sKeyBoxShown(), dispboxdragger );

    if ( as_.usePar( par ) )
        updateSlice();

    turnOn( true );
    showBoxDragger( dispboxdragger );

    return true;
}


visBase::OrthogonalSlice* MPEDisplay::getSlice( int index )
{
    return index>=0 && index<slices_.size() ? slices_[index] : 0;
}


void MPEDisplay::alignSliceToSurvey( visBase::OrthogonalSlice& slice )
{
    Coord3 center = slice.getDragger()->center();

    if ( slice.getDim() == cInLine() )
	center.x = SI().inlRange(true).snap( center.x );
    if ( slice.getDim() == cCrossLine() )
	center.y = SI().crlRange(true).snap( center.y );
    if ( slice.getDim() == cTimeSlice() )
	center.z = SI().zRange(true).snap( center.z );

    slice.setCenter( center, false );
}


void MPEDisplay::setCubeSampling( const CubeSampling& cs )
{
    const Interval<float> xintv( mCast(float,cs.hrg.start.inl()),
				    mCast(float,cs.hrg.stop.inl()) );
    const Interval<float> yintv( mCast(float,cs.hrg.start.crl()),
				    mCast(float,cs.hrg.stop.crl()) );
    const Interval<float> zintv( cs.zrg.start, cs.zrg.stop );

    for ( int idx=0; idx<slices_.size(); idx++ )
    {
	slices_[idx]->setSpaceLimits( xintv, yintv, zintv );
	alignSliceToSurvey( *slices_[idx] );
    }

    curboxcenter_ = Coord3( xintv.center(), yintv.center(), zintv.center() );
    curboxwidth_ = Coord3( xintv.width(), yintv.width(), zintv.width() );
    resetManipulation();
}


void MPEDisplay::resetManipulation()
{
    boxdragger_->setCenter( curboxcenter_ );
    boxdragger_->setWidth( curboxwidth_ );
}


bool MPEDisplay::setDataPackID( int attrib, DataPack::ID dpid,
				   TaskRunner* tr )
{
    if ( attrib != 0 || dpid == DataPack::cNoID() ) return false;

    DataPackMgr& dpman = DPM( DataPackMgr::CubeID() );
    const DataPack* datapack = dpman.obtain( dpid );
    mDynamicCastGet(const Attrib::CubeDataPack*,cdp,datapack);

    const bool res = setDataVolume( attrib, cdp, tr );
    if ( !res )
    {
	dpman.release( dpid );
	return false;
    }

    if ( volumecache_ != cdp )
    {
	if (volumecache_ )
	    dpman.release( volumecache_ );
	volumecache_ = cdp;
    }

    return true;
}


bool MPEDisplay::setDataVolume( int attrib, const Attrib::CubeDataPack* cdp,
				   TaskRunner* tr )
{
    if ( !cdp )
	return false;

    DataPack::ID attrib_dpid = cdp->id();
    DPM( DataPackMgr::CubeID() ).obtain( attrib_dpid );

    //transform data if necessary.
    const char* zdomain = getSelSpec( attrib )->zDomainKey();
    const bool alreadytransformed = zdomain && *zdomain;

    if ( !alreadytransformed && datatransform_ )
    {
	// to do: check this stuff
	ZAxisTransformer* datatransformer;
	mTryAlloc( datatransformer,ZAxisTransformer(*datatransform_,true));
	datatransformer->setInterpolate( textureInterpolationEnabled() );
	//datatransformer->setInterpolate( true );
	//datatransformer->setInput( cdp->cube().getCube(0), cdp->sampling() );
	datatransformer->setInput( cdp->cube().getCube(0),
		cdp->cube().cubeSampling() );
	datatransformer->setOutputRange( getCubeSampling(true,true,0) );

	if ( TaskRunner::execute( tr, *datatransformer ) )
	{
	    pErrMsg( "Transform failed" );
	    return false;
	}

	CubeDataPack cdpnew( cdp->categoryStr( false ),
			// check false for categoryStr
		datatransformer->getOutput( true ) );
	DPM( DataPackMgr::CubeID() ).addAndObtain( &cdpnew );
	DPM( DataPackMgr::CubeID() ).release( attrib_dpid );
	attrib_dpid = cdpnew.id();
    }

    DPM(DataPackMgr::CubeID()).release( cacheid_ );
    cacheid_ = attrib_dpid;

    bool retval = updateFromCacheID( attrib, tr );
    if ( !retval )
	channels_->turnOn( false );

    DPM( DataPackMgr::CubeID() ).release( attrib_dpid );

    setCubeSampling( getCubeSampling(true,true,0) );

    return retval;
}


bool MPEDisplay::updateFromCacheID( int attrib, TaskRunner* tr )
{
    channels_->setNrVersions( attrib, 1 );

    ConstRefMan<Attrib::DataCubes> attrdata = engine_.getAttribCache( as_ ) ?
	engine_.getAttribCache( as_ )->get3DData() : 0;
    if ( !attrdata )
	return false;

    const Array3D<float>& data( attrdata->getCube(0) );
    const float* arr = data.getData();

    OD::PtrPolicy cp = OD::UsePtr;

    // get the dimensions from the engine and then get a subsample of the array
    const CubeSampling displaycs = engine_.activeVolume();
    if ( displaycs != attrdata->cubeSampling() )
    {
	const CubeSampling attrcs = attrdata->cubeSampling();

	if ( !attrcs.includes( displaycs ) )
	    return false;

	const StepInterval<int> inlrg( attrcs.hrg.inlRange() );
	const StepInterval<int> crlrg( attrcs.hrg.crlRange() );
	const Interval<int> dispinlrg(inlrg.getIndex(displaycs.hrg.start.inl()),
				      inlrg.getIndex(displaycs.hrg.stop.inl()));
	const Interval<int> dispcrlrg(crlrg.getIndex(displaycs.hrg.start.crl()),
				      crlrg.getIndex(displaycs.hrg.stop.crl()));

	const StepInterval<float>& zrg( displaycs.zrg );
	const Interval<int> dispzrg( attrcs.zrg.nearestIndex( zrg.start ),
				     attrcs.zrg.nearestIndex( zrg.stop ) );

	const int sz0 = dispinlrg.width()+1;
	const int sz1 = dispcrlrg.width()+1;
	const int sz2 = dispzrg.width()+1;

	const Array3DSubSelection<float> arrsubsel(
		dispinlrg.start, dispcrlrg.start, dispzrg.start,
		sz0, sz1, sz2,
		const_cast< Array3D<float>& >(data) );

	if ( !arrsubsel.isOK() )
	    return false;

	arr = arrsubsel.getData();

	if ( !arr )
        {
	    mDeclareAndTryAlloc( float*, tmparr, float[sz0 * sz1 * sz2] );

	    if ( !tmparr )
		return false;
	    else
	    {
		arrsubsel.getAll( tmparr );
		arr = tmparr;
		cp = OD::TakeOverPtr;
	    }
	}
	channels_->setSize( attrib, sz0, sz1, sz2 );
	for ( int idx=0; idx<slices_.size(); idx++ )
	    slices_[idx]->setVolumeDataSize( sz0, sz1, sz2 );
    }

    channels_->setUnMappedData( attrib, 0, arr, cp, tr );
    channels_->reMapData( 0, false, 0 );

    setCubeSampling( getCubeSampling(true,true,0) );

    channels_->turnOn( true );
    if ( !slices_.isEmpty() )
	slices_[0]->turnOn( true );

    return true;
}


void MPEDisplay::updateSlice()
{
    const CubeSampling displaycs = engine_.activeVolume();

    if ( curtextureas_==as_ && curtexturecs_==displaycs )
    {
	if ( !slices_.isEmpty() )
	    slices_[0]->turnOn( true );
        return;
    }

    if ( ! setDataPackID( 0, engine_.getAttribCacheID( as_ ), 0 ) )
    {
	turnOnSlice( false );
	curtexturecs_=0;
	return;
    }

    curtextureas_ = as_;
    curtexturecs_ = displaycs;

    turnOnSlice( true );
}


const Attrib::DataCubes* MPEDisplay::getCacheVolume( int attrib ) const
{
    return ( volumecache_ && !attrib ) ? &volumecache_->cube() : 0;
}


DataPack::ID MPEDisplay::getDataPackID( int attrib ) const
{
    return ( !attrib ) ? cacheid_ : DataPack::cNoID();
}


CubeSampling MPEDisplay::getCubeSampling( bool manippos, bool displayspace,
					  int attrib ) const
{
    CubeSampling res;
    if ( manippos )
    {
	Coord3 center = boxdragger_->center();
	Coord3 width = boxdragger_->width();

	res.hrg.start = BinID( mNINT32( center.x - width.x / 2 ),
		mNINT32( center.y - width.y / 2 ) );

	res.hrg.stop = BinID( mNINT32( center.x + width.x / 2 ),
		mNINT32( center.y + width.y / 2 ) );

	res.hrg.step = BinID( SI().inlStep(), SI().crlStep() );

	res.zrg.start = (float) ( center.z - width.z / 2 );
	res.zrg.stop = (float) ( center.z + width.z / 2 );
    }
    else
    {
	const Coord3 transl = curboxcenter_;
	const Coord3 scale = curboxwidth_;

	res.hrg.start = BinID( mNINT32(transl.x+scale.x/2),
		mNINT32(transl.y+scale.y/2) );
	res.hrg.stop = BinID( mNINT32(transl.x-scale.x/2),
		mNINT32(transl.y-scale.y/2) );
	res.hrg.step = BinID( SI().inlStep(), SI().crlStep() );

	res.zrg.start = (float) ( transl.z+scale.z/2 );
	res.zrg.stop = (float) ( transl.z-scale.z/2 );
    }

    if ( alreadyTransformed(attrib) ) return res;

    if ( datatransform_ && !displayspace )
    {
	res.zrg.setFrom( datatransform_->getZInterval(true) );
	res.zrg.step = SI().zRange( true ).step;
    }

    return res;
}


void MPEDisplay::setSliceDimension( int sliceidx, int dim )
{
    if ( slices_.validIdx(sliceidx) && dim>=0 && dim<3 )
    {
	slices_[sliceidx]->setDim( dim );
	slices_[sliceidx]->setName( dim==cTimeSlice() ? sKeyTime() :
				    dim==cCrossLine() ? sKeyCrossLine() :
							sKeyInline() );
    }
}


int MPEDisplay::addSlice( int dim, bool show )
{
    visBase::OrthogonalSlice* slice = visBase::OrthogonalSlice::create();
    slice->ref();
    slice->setDisplayTransformation( displaytrans_ );
    slice->turnOn( show );
    // slice->setMaterial(0);
    slice->motion.notify( mCB(this,MPEDisplay,sliceMoving) );
    slices_ += slice;
    setSliceDimension( slices_.size()-1, dim );

    addChild( slice->osgNode() );
    setCubeSampling( getCubeSampling(true,true,0) );
    alignSliceToSurvey( *slice );

    if ( volumecache_ )
    {
	const Array3D<float>& arr = volumecache_->cube().getCube(0);
	slice->setVolumeDataSize( arr.info().getSize(2),
		arr.info().getSize(1), arr.info().getSize(0) );
    }

    slice->setTextureChannels( channels_ );

    return slice->id();
}


float MPEDisplay::slicePosition( visBase::OrthogonalSlice* slice ) const
{
    if ( !slice ) return 0;
    const int dim = slice->getDim();
    float slicepos = slice->getPosition();

    float pos = mCast( float, SI().inlRange(true).snap(slicepos) );
    if ( dim == cCrossLine() )
	pos = mCast( float, SI().crlRange(true).snap(slicepos) );
    else if ( dim == cTimeSlice() )
	pos = mCast( float, mNINT32(slicepos*1000) );

    return pos;
}


float MPEDisplay::getValue( const Coord3& pos_ ) const
{
    if ( !volumecache_ ) return mUdf(float);
    const BinIDValue bidv( SI().transform(pos_), (float) pos_.z );
    float val;
    if ( !volumecache_->cube().getValue(0,bidv,&val,false) )
        return mUdf(float);

    return val;
}


void MPEDisplay::allowShading( bool yn )
{
    if ( channels_ && channels_->getChannels2RGBA() )
	channels_->getChannels2RGBA()->allowShading( yn );
}


void MPEDisplay::removeChild( int displayid )
{
    for ( int idx=0; idx<slices_.size(); idx++ )
    {
	if ( slices_[idx]->id()==displayid )
	{
	    VisualObjectImpl::removeChild( slices_[idx]->osgNode() );
	    slices_[idx]->motion.remove( mCB(this,MPEDisplay,sliceMoving) );
	    slices_[idx]->unRef();
	    slices_.removeSingle(idx,false);
	    return;
	}
    }
}


void MPEDisplay::getChildren( TypeSet<int>&res ) const
{
    res.erase();
    for ( int idx=0; idx<slices_.size(); idx++ )
	res += slices_[idx]->id();
}


bool MPEDisplay::isSelected() const
{
    return visBase::DM().selMan().selected().isPresent( id());
}


BufferString MPEDisplay::getManipulationString() const
{
    BufferString res;
    getObjectInfo( res );
    return res;
}


void MPEDisplay::getObjectInfo( BufferString& info ) const
{
    info = slicename_; info += ": "; info += sliceposition_;
}


void MPEDisplay::sliceMoving( CallBacker* cb )
{
    mDynamicCastGet(visBase::OrthogonalSlice*,slice,cb);
    if ( !slice ) return;

    slicename_ = slice->name();
    sliceposition_ = slicePosition( slice );

    if ( isSelected() ) return;

    while ( true )
    {
	MPE::TrackPlane newplane = engine_.trackPlane();
	CubeSampling& planebox = newplane.boundingBox();
	// get the position of the plane from the dragger
	getPlanePosition( planebox );

	// nothing to do if dragger is in sync with engine
	if ( planebox==engine_.trackPlane().boundingBox() )
	    return;

	const CubeSampling& engineplane = engine_.trackPlane().boundingBox();
	const int dim = slice->getDragger()->getDim();
	if ( dim==cInLine() &&
	     planebox.hrg.start.inl()==engineplane.hrg.start.inl() )
	    return;
	if ( dim==cCrossLine() &&
	     planebox.hrg.start.crl()==engineplane.hrg.start.crl() )
	    return;
	if ( dim==cTimeSlice() &&
	     mIsEqual(planebox.zrg.start, engineplane.zrg.start,
		      0.1*SI().zStep()) )
	    return;

	if ( dim==cInLine() )
	{
	    const bool inc =
		planebox.hrg.start.inl()>engineplane.hrg.start.inl();
	    int& start = planebox.hrg.start.inl();
	    int& stop =  planebox.hrg.stop.inl();
	    const int step = SI().inlStep();
	    start = stop = engineplane.hrg.start.inl() + ( inc ? step : -step );
	    newplane.setMotion( inc ? step : -step, 0, 0 );
	}
	else if ( dim==cCrossLine() )
	{
	    const bool inc =
		planebox.hrg.start.crl()>engineplane.hrg.start.crl();
	    int& start = planebox.hrg.start.crl();
	    int& stop =  planebox.hrg.stop.crl();
	    const int step = SI().crlStep();
	    start = stop = engineplane.hrg.start.crl() + ( inc ? step : -step );
	    newplane.setMotion( 0, inc ? step : -step, 0 );
	}
	else if ( dim==cTimeSlice() )
	{
	    const bool inc = planebox.zrg.start>engineplane.zrg.start;
	    float& start = planebox.zrg.start;
	    float& stop =  planebox.zrg.stop;
	    const double step = SI().zStep();
	    start = stop = (float) ( engineplane.zrg.start +
						( inc ? step : -step ) );
	    newplane.setMotion( 0, 0, (float) ( inc ? step : -step ) );
	}

	const MPE::TrackPlane::TrackMode trkmode = newplane.getTrackMode();
	engine_.setTrackPlane( newplane, trkmode==MPE::TrackPlane::Extend
				      || trkmode==MPE::TrackPlane::ReTrack
				      || trkmode==MPE::TrackPlane::Erase );
	movement.trigger();
	planeOrientationChange.trigger();
    }
}


void MPEDisplay::showManipulator( bool yn )
{
    showBoxDragger( yn );
}


bool MPEDisplay::isManipulated() const
{
    return getCubeSampling(true,true,0) != getCubeSampling(false,true,0);
}


bool MPEDisplay::canResetManipulation() const
{
    return true;
}


void MPEDisplay::acceptManipulation()
{
    setCubeSampling( getCubeSampling(true,true,0) );
}


bool MPEDisplay::allowsPicks() const
{
    return true;
}


void MPEDisplay::turnOnSlice( bool yn )
{
    if ( !slices_.isEmpty() )
	slices_[0]->turnOn( yn );
}


bool MPEDisplay::setZAxisTransform( ZAxisTransform* zat, TaskRunner* tr )
{
    const bool haddatatransform = datatransform_;
    if ( datatransform_ )
    {
	if ( datatransform_->changeNotifier() )
	    datatransform_->changeNotifier()->remove(
		    mCB(this,MPEDisplay,dataTransformCB) );
	datatransform_->unRef();
	datatransform_ = 0;
    }

    datatransform_ = zat;

    if ( datatransform_ )
    {
	datatransform_->ref();
	updateRanges( false, !haddatatransform );
	if ( datatransform_->changeNotifier() )
	    datatransform_->changeNotifier()->notify(
		    mCB(this,MPEDisplay,dataTransformCB) );
    }

    return true;
}


const ZAxisTransform* MPEDisplay::getZAxisTransform() const
{
    return datatransform_;
}


void MPEDisplay::dataTransformCB( CallBacker* )
{
    updateRanges( false, true );
    if ( volumecache_)
	setDataVolume( 0, volumecache_, 0 );
}

void MPEDisplay::triggerSel()
{
    mouseClickCB( 0 );
    visBase::VisualObject::triggerSel();
}


void MPEDisplay::triggerDeSel()
{
    mouseClickCB( 0 );
    visBase::VisualObject::triggerDeSel();
}


void MPEDisplay::updateRanges( bool updateic, bool updatez )
{
    if ( !datatransform_ ) return;

    if ( csfromsession_ != SI().sampling(true) )
	setCubeSampling( csfromsession_ );
    else
    {
	Interval<float> zrg = datatransform_->getZInterval( false );
	CubeSampling cs = getCubeSampling( 0 );
	assign( cs.zrg, zrg );
	setCubeSampling( cs );
    }
}


SurveyObject::AttribFormat MPEDisplay::getAttributeFormat( int attrib ) const
{
    return !attrib ? SurveyObject::Cube : SurveyObject::None;
}


int MPEDisplay::nrAttribs() const
{
    return ( as_.id().asInt() == Attrib::SelSpec::cNoAttrib().asInt() ) ? 0 : 1;
}


bool MPEDisplay::canAddAttrib( int nr ) const
{
    return ( nr + nrAttribs() <= 1 ) ? true : false;
}


bool MPEDisplay::canRemoveAttrib() const
{
    return ( nrAttribs() == 1 ) ? true : false;
}


bool MPEDisplay::addAttrib()
{
    BufferStringSet* attrnms = new BufferStringSet();
    attrnms->allowNull();
    userrefs_ += attrnms;
    as_.set( "", Attrib::SelSpec::cAttribNotSel(), false, 0 );
    channels_->addChannel();
    return true;
}


bool MPEDisplay::removeAttrib( int attrib )
{
    channels_->removeChannel( attrib );
    as_.set( "", Attrib::SelSpec::cNoAttrib(), false, 0 );
    userrefs_.removeSingle( attrib );
    return true;
}


bool MPEDisplay::isAttribEnabled( int attrib ) const
{
    return attrib ? false : channels_->getChannels2RGBA()->isEnabled( attrib );
}


void MPEDisplay::enableAttrib( int attrib, bool yn )
{
    if ( !attrib )
	channels_->getChannels2RGBA()->setEnabled( attrib, yn );
}


void MPEDisplay::setDisplayTransformation( const mVisTrans* trans )
{
    displaytrans_ = trans;
    boxdragger_->setDisplayTransformation( trans );

    for ( int idx=0; idx<slices_.size(); idx++ )
	slices_[idx]->setDisplayTransformation( trans );
}


void MPEDisplay::enablePicking( bool yn )
{
    for ( int idx=0; idx<slices_.size(); idx++ )
	slices_[idx]->enablePicking( yn );
}


bool MPEDisplay::isPickingEnabled() const
{
    return !slices_.isEmpty() && slices_[0]->isPickingEnabled();
}


}; // namespace vissurvey

