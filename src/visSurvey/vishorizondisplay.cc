/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:        D. Zheng
 Date:          Feb 2013
________________________________________________________________________

-*/
static const char* rcsID mUsedVar = "$Id$";

#include "vishorizondisplay.h"

#include "arrayndimpl.h"
#include "attribsel.h"
#include "bidvsetarrayadapter.h"
#include "binidvalue.h"
#include "coltabmapper.h"
#include "coltabsequence.h"
#include "datapointset.h"
#include "datacoldef.h"
#include "emhorizon3d.h"
#include "emobjectposselector.h"
#include "emsurfaceauxdata.h"
#include "isocontourtracer.h"
#include "settings.h"
#include "survinfo.h"
#include "mpeengine.h"
#include "posvecdataset.h"
#include "callback.h"

#include "visevent.h"
#include "vismarkerset.h"
#include "vismaterial.h"
#include "vismpe.h"
#include "vishorizontexturehandler.h"
#include "vishorizonsection.h"
#include "vishorizonsectiondef.h"
#include "visplanedatadisplay.h"
#include "vispointset.h"
#include "vispolyline.h"
#include "visrandomtrackdisplay.h"
#include "vistexturechannel2rgba.h"
#include "vistexturechannels.h"
#include "vismultiattribsurvobj.h"
#include "visseis2ddisplay.h"
#include "vistransform.h"
#include "zaxistransform.h"



using namespace visSurvey;

const char* HorizonDisplay::sKeyTexture()	{ return "Use texture"; }
const char* HorizonDisplay::sKeyShift()		{ return "Shift"; }
const char* HorizonDisplay::sKeyResolution()	{ return "Resolution"; }
const char* HorizonDisplay::sKeyRowRange()	{ return "Row range"; }
const char* HorizonDisplay::sKeyColRange()	{ return "Col range"; }
const char* HorizonDisplay::sKeySurfaceGrid()	{ return "SurfaceGrid"; }
const char* HorizonDisplay::sKeyIntersectLineMaterialID()
{ return "Intsectline material id"; }
const char* HorizonDisplay::sKeySectionID()	{ return "Section ID"; }
const char* HorizonDisplay::sKeyZValues()	{ return "Z values"; }


HorizonDisplay::HorizonDisplay()
    : parrowrg_( -1, -1, -1 )
    , parcolrg_( -1, -1, -1 )
    , curtextureidx_( 0 )
    , usestexture_( true )
    , translation_( 0 )
    , validtexture_( false )
    , resolution_( 0 )
    , allowshading_( true )
    , intersectionlinematerial_( 0 )
    , displayintersectionlines_( true )
    , enabletextureinterp_( true )
    , displaysurfacegrid_( false )
    , translationpos_( Coord3().udf() )
    , parentline_(0)
    , selections_(0)
    , lockedpts_(0)
{
    translation_ = visBase::Transformation::create();
    translation_->ref();

    setGroupNode( (osg::Group*) translation_->osgNode() );

    setLockable();
    maxintersectionlinethickness_ = 0.02f *
	mMAX( SI().inlDistance() * SI().inlRange(true).width(),
	      SI().crlDistance() * SI().crlRange(true).width() );

    as_ += new Attrib::SelSpec;
    coltabmappersetups_ += ColTab::MapperSetup();
    coltabsequences_ += ColTab::Sequence(ColTab::defSeqName());

    TypeSet<float> shift;
    shift += 0.0;
    curshiftidx_ += 0;
    BufferStringSet* attrnms = new BufferStringSet();
    attrnms->allowNull();
    userrefs_.addIfNew( attrnms );
    shifts_ += new TypeSet<float>;
    enabled_ += true;
    dispdatapackids_ += new TypeSet<DataPack::ID>;

    material_->setAmbience( 0.7 );

    RefMan<visBase::Material> linemat = new visBase::Material;
    linemat->setFrom( *material_ );
    linemat->setColor( nontexturecol_ );
    linemat->setDiffIntensity( 1 );
    linemat->setAmbience( 1 );
    setIntersectLineMaterial( linemat );

    int res = (int)resolution_;
    Settings::common().get( "dTect.Horizon.Resolution", res );
    resolution_ = (char)res;
}


HorizonDisplay::~HorizonDisplay()
{
    deepErase(as_);
    unRefAndZeroPtr( intersectionlinematerial_ );

    coltabmappersetups_.erase();
    coltabsequences_.erase();

    setSceneEventCatcher( 0 );
    curshiftidx_.erase();

   if ( translation_ )
    {
	translation_->unRef();
	translation_ = 0;
    }

    removeEMStuff();
    unRefAndZeroPtr( zaxistransform_ );

    DataPackMgr& dpm = DPM(DataPackMgr::FlatID());
    for ( int idx=0; idx<dispdatapackids_.size(); idx++ )
    {
	const TypeSet<DataPack::ID>& dpids = *dispdatapackids_[idx];
	for ( int idy=dpids.size()-1; idy>=0; idy-- )
	    dpm.release( dpids[idy] );
    }

    deepErase( dispdatapackids_ );
    deepErase( shifts_ );

    if ( selections_ ) selections_->unRef();
    if ( lockedpts_ ) lockedpts_->unRef();
}


void HorizonDisplay::displayIntersectionLines( bool yn )
{
    displayintersectionlines_ = yn;

    hasmoved.trigger();
    changedisplay.trigger();
}


bool HorizonDisplay::displaysIntersectionLines() const
{
    return displayintersectionlines_;
}


void HorizonDisplay::setDisplayTransformation( const mVisTrans* nt )
{
    EMObjectDisplay::setDisplayTransformation( nt );

    MouseCursorChanger cursorchanger( MouseCursor::Wait );

    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->setDisplayTransformation(transformation_);

    for ( int idx=0; idx<intersectiondata_.size(); idx++ )
	intersectiondata_[idx]->setDisplayTransformation(transformation_);

    if ( translationpos_.isDefined() )
	setTranslation( translationpos_ );

    if ( parentline_ )
	parentline_->setDisplayTransformation( transformation_ );
    if ( selections_ )
	selections_->setDisplayTransformation( transformation_ );
    if ( lockedpts_ )
	lockedpts_->setDisplayTransformation( transformation_ );

}


bool HorizonDisplay::setZAxisTransform( ZAxisTransform* nz, TaskRunner* trans )
{
    if ( zaxistransform_ ) zaxistransform_->unRef();

    zaxistransform_ = nz;
    if ( zaxistransform_ ) zaxistransform_->ref();

    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->setZAxisTransform( nz, trans );

    return true;
}


bool HorizonDisplay::setChannels2RGBA( visBase::TextureChannel2RGBA* t )
{
    RefMan<visBase::TextureChannel2RGBA> dummy( t );
    if ( sections_.size()!=1 )
	return EMObjectDisplay::setChannels2RGBA( t );

    EMObjectDisplay::setChannels2RGBA( 0 );
    sections_[0]->setChannels2RGBA( t );

    return true;
}


visBase::TextureChannel2RGBA* HorizonDisplay::getChannels2RGBA()
{
    return sections_.size()
	? sections_[0]->getChannels2RGBA()
	: EMObjectDisplay::getChannels2RGBA();
}


const visBase::TextureChannel2RGBA* HorizonDisplay::getChannels2RGBA() const
{ return const_cast<HorizonDisplay*>(this)->getChannels2RGBA(); }


void HorizonDisplay::setSceneEventCatcher(visBase::EventCatcher* ec)
{
    EMObjectDisplay::setSceneEventCatcher( ec );

    for ( int idx=0; idx<sections_.size(); idx++ )
    {
	sections_[idx]->setSceneEventCatcher( ec );
	if ( ec && scene_ )
	    sections_[idx]->setRightHandSystem( scene_->isRightHandSystem() );
    }

    for ( int idx=0; idx<intersectiondata_.size(); idx++ )
	intersectiondata_[idx]->setSceneEventCatcher( ec );
}


EM::PosID HorizonDisplay::findClosestNode( const Coord3& pickedpos ) const
{
    const mVisTrans* ztrans = scene_->getTempZStretchTransform();
    Coord3 newpos;
    ztrans->transformBack( pickedpos, newpos );
    if ( transformation_ )
	transformation_->transformBack( newpos );

    const BinID pickedbid = SI().transform( newpos );
    const EM::SubID pickedsubid = pickedbid.toInt64();
    TypeSet<EM::PosID> closestnodes;
    for ( int idx=sids_.size()-1; idx>=0; idx-- )
    {
	if ( !emobject_->isDefined( sids_[idx], pickedsubid ) )
	    continue;
	closestnodes += EM::PosID( emobject_->id(), sids_[idx], pickedsubid );
    }

    if ( closestnodes.isEmpty() ) return EM::PosID( -1, -1, -1 );

    EM::PosID closestnode = closestnodes[0];
    float mindist = mUdf(float);
    for ( int idx=0; idx<closestnodes.size(); idx++ )
    {
	const Coord3 coord = emobject_->getPos( closestnodes[idx] );
	Coord3 displaypos;
	mVisTrans::transform( transformation_, coord, displaypos );
	mVisTrans::transform( ztrans, displaypos );

	const float dist = (float) displaypos.distTo( pickedpos );
	if ( !idx || dist<mindist )
	{
	    closestnode = closestnodes[idx];
	    mindist = dist;
	}
    }

    return closestnode;
}


void HorizonDisplay::removeEMStuff()
{
    for ( int idx=0; idx<sections_.size(); idx++ )
    {
	removeChild( sections_[idx]->osgNode() );
	sections_[idx]->unRef();
    }

    sections_.erase();
    sids_.erase();
    intersectiondata_.erase();
    EMObjectDisplay::removeEMStuff();
}


bool HorizonDisplay::setEMObject( const EM::ObjectID& newid, TaskRunner* trnr )
{
    return EMObjectDisplay::setEMObject( newid, trnr );
}


StepInterval<int> HorizonDisplay::geometryRowRange() const
{
    mDynamicCastGet(const EM::Horizon3D*, surface, emobject_ );
    if ( !surface ) return parrowrg_;

    return surface->geometry().rowRange();
}


StepInterval<int> HorizonDisplay::geometryColRange() const
{
    mDynamicCastGet(const EM::Horizon3D*,horizon3d,emobject_);
    return horizon3d ? horizon3d->geometry().colRange() : parcolrg_;
}


bool HorizonDisplay::updateFromEM( TaskRunner* trans )
{
    if ( !EMObjectDisplay::updateFromEM( trans ) )
	return false;
    updateSingleColor();
    return true;
}


void HorizonDisplay::updateFromMPE()
{
    if ( geometryRowRange().nrSteps()<=1 || geometryColRange().nrSteps()<=1 )
	setResolution( 0, 0 ); //Automatic resolution
    EMObjectDisplay::updateFromMPE();
}


void HorizonDisplay::useTexture( bool yn, bool trigger )
{
    if ( yn && !validtexture_ )
    {
	for ( int idx=0; idx<nrAttribs(); idx++ )
	{
	    if ( as_[idx]->id().asInt() == Attrib::SelSpec::cNoAttrib().asInt())
	    {
		usestexture_ = yn;
		setDepthAsAttrib(idx);
		return;
	    }
	}
    }

    usestexture_ = yn;

    updateSingleColor();

    if ( trigger )
	changedisplay.trigger();
}


bool HorizonDisplay::usesTexture() const
{ return usestexture_; }


bool HorizonDisplay::canShowTexture() const
{
    return validtexture_ && isAnyAttribEnabled() && !displayedOnlyAtSections();
}


bool HorizonDisplay::showsTexture() const
{
    return canShowTexture() && usesTexture();
}


bool HorizonDisplay::displayedOnlyAtSections() const
{ return displayonlyatsections_; }


bool HorizonDisplay::canHaveMultipleAttribs() const
{ return true; }


int HorizonDisplay::nrTextures( int channel ) const
{
    if ( channel<0 || channel>=nrAttribs() || !sections_.size() )
	return 0;

    return sections_[0]->nrVersions( channel );
}


void HorizonDisplay::selectTexture( int channel, int textureidx )
{
    curtextureidx_ = textureidx;
    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->selectActiveVersion( channel, textureidx );

    if ( !as_.validIdx(channel) || !userrefs_.validIdx(channel) ||
	 userrefs_[channel]->isEmpty() )
	return;

    if ( userrefs_[channel]->get(0) == sKeySectionID() )
	textureidx++;

    BufferString usrref = userrefs_[channel]->validIdx(textureidx) ?
	userrefs_[channel]->get(textureidx) : "<No name>";
    as_[channel]->setUserRef( usrref );
}


int HorizonDisplay::selectedTexture( int channel ) const
{
    if ( channel<0 || channel>=nrAttribs() || !sections_.size() )
	return 0;

    return sections_[0]->activeVersion( channel );
}


SurveyObject::AttribFormat HorizonDisplay::getAttributeFormat( int ) const
{
    return sections_.size() ? SurveyObject::RandomPos : SurveyObject::None;
}


int HorizonDisplay::nrAttribs() const
{ return as_.size(); }


bool HorizonDisplay::canAddAttrib( int nr ) const
{
    if ( !sections_.size() )
	return false;

    const int maxnr =  sections_[0]->getChannels2RGBA()->maxNrChannels();
    if ( !maxnr ) return true;

    return nrAttribs()+nr<=maxnr;
}


bool HorizonDisplay::canRemoveAttrib() const
{
    if ( !sections_.size() )
	return false;

    const int newnrattribs = nrAttribs()-1;
    return newnrattribs>=sections_[0]->getChannels2RGBA()->minNrChannels();
}


void HorizonDisplay::setAttribShift( int channel, const TypeSet<float>& shifts )
{
    (*shifts_[channel]) = shifts;
}


bool HorizonDisplay::addAttrib()
{
    as_ += new Attrib::SelSpec;
    TypeSet<float> shift;
    shift += 0.0;
    curshiftidx_ += 0;
    BufferStringSet* attrnms = new BufferStringSet();
    attrnms->allowNull();
    userrefs_ += attrnms;
    enabled_ += true;
    shifts_ += new TypeSet<float>;
    dispdatapackids_ += new TypeSet<DataPack::ID>;
    coltabmappersetups_ += ColTab::MapperSetup();
    coltabsequences_ += ColTab::Sequence(ColTab::defSeqName());

    const int curchannel = coltabmappersetups_.size()-1;
    for ( int idx=0; idx<sections_.size(); idx++ )
    {
	sections_[idx]->addChannel();
	sections_[idx]->setColTabSequence( curchannel,
		coltabsequences_[curchannel] );

	sections_[idx]->setColTabMapperSetup( curchannel,
		coltabmappersetups_[curchannel], 0 );
    }

    return true;
}


bool HorizonDisplay::removeAttrib( int channel )
{
    if ( channel<0 || channel>=nrAttribs() )
       return true;

    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->removeChannel( channel );

    curshiftidx_.removeSingle( channel );
    userrefs_.removeSingle( channel );
    enabled_.removeSingle( channel );
    delete shifts_.removeSingle( channel );

    const TypeSet<DataPack::ID>& dpids = *dispdatapackids_[channel];
    for ( int idy=dpids.size()-1; idy>=0; idy-- )
	DPM(DataPackMgr::FlatID()).release( dpids[idy] );
    delete dispdatapackids_.removeSingle( channel );

    coltabmappersetups_.removeSingle( channel );
    coltabsequences_.removeSingle( channel );
    delete as_.removeSingle( channel );

    for ( int chan=channel; chan<nrAttribs(); chan++ )
    {
	for ( int idx=0; idx<sections_.size(); idx++ )
	    sections_[idx]->setColTabSequence( chan, coltabsequences_[chan] );
    }

    return true;
}


bool HorizonDisplay::swapAttribs( int a0, int a1 )
{
    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->swapChannels( a0, a1 );

    coltabmappersetups_.swap( a0, a1 );
    coltabsequences_.swap( a0, a1 );

    as_.swap( a0, a1 );
    enabled_.swap( a0, a1 );
    shifts_.swap( a0, a1 );
    curshiftidx_.swap( a0, a1 );
    userrefs_.swap( a0, a1 );
    dispdatapackids_.swap( a0, a1 );
    return true;
}


void HorizonDisplay::setAttribTransparency( int channel, unsigned char nt )
{
    if ( channel<0 || channel>=nrAttribs() )
       return;

    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->setTransparency( channel, nt );
}


unsigned char HorizonDisplay::getAttribTransparency( int channel ) const
{
    if ( channel<0 || channel>=nrAttribs() )
       return 0;

    return sections_.size() ? sections_[0]->getTransparency(channel) : 0;
}


void HorizonDisplay::enableAttrib( int channelnr, bool yn )
{
    enabled_[channelnr] = yn;
    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->getChannels2RGBA()->setEnabled( channelnr, yn );

    updateSingleColor();
}


bool HorizonDisplay::isAttribEnabled( int channel ) const
{
    if ( channel<0 || channel>=nrAttribs() )
       return false;

    return enabled_[channel];
}


void HorizonDisplay::allowShading( bool yn )
{
    allowshading_ = yn;
    for ( int idx=sections_.size()-1; idx>=0; idx-- )
	sections_[idx]->getChannels2RGBA()->allowShading( yn );

}


const Attrib::SelSpec* HorizonDisplay::getSelSpec( int channel ) const
{ return as_.validIdx( channel ) ? as_[channel] : 0; }


void HorizonDisplay::setSelSpec( int channel, const Attrib::SelSpec& as )
{
    if ( as_.validIdx(channel) && as_[channel] )
	(*as_[channel]) = as;
}


void HorizonDisplay::setDepthAsAttrib( int channel )
{
    if ( !as_.validIdx(channel) )
	return;

    const bool attribwasdepth =
			FixedString(as_[channel]->userRef()) == sKeyZValues();

    as_[channel]->set( sKeyZValues(), Attrib::SelSpec::cNoAttrib(), false, "" );

    TypeSet<DataPointSet::DataRow> pts;
    ObjectSet<DataColDef> defs;
    DataPointSet positions( pts, defs, false, true );
    getRandomPos( positions, 0 );

    const DataColDef zvalsdef( sKeyZValues() );
    if ( positions.dataSet().findColDef(zvalsdef,PosVecDataSet::NameExact)==-1 )
	positions.dataSet().add( new DataColDef(zvalsdef) );

    if ( !positions.size() ) return;

    BinIDValueSet& bivs = positions.bivSet();
    if ( bivs.nrVals()!=3 )
    {
	pErrMsg( "Hmm" );
	return;
    }

    int zcol= positions.dataSet().findColDef(zvalsdef,PosVecDataSet::NameExact);
    if ( zcol==-1 )
	zcol = 2;

    BinIDValueSet::SPos pos;
    while ( bivs.next(pos,true) )
    {
	float* vals = bivs.getVals(pos);
	if ( zaxistransform_ )
	{
	    vals[zcol] = zaxistransform_->transform(
		    BinIDValue( bivs.getBinID(pos), vals[0] ) );
	}
	else
	    vals[zcol] = vals[0];
    }

    setRandomPosData( channel, &positions, 0 );

    if ( !attribwasdepth )
    {
	BufferString seqnm;
	Settings::common().get( "dTect.Horizon.Color table", seqnm );
	ColTab::Sequence seq( seqnm );
	setColTabSequence( channel, seq, 0 );
	setColTabMapperSetup( channel, ColTab::MapperSetup(), 0 );
    }
}


void HorizonDisplay::getRandomPos( DataPointSet& data, TaskRunner* trans ) const
{
    //data.bivSet().allowDuplicateBids(false);
    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->getDataPositions( data, getTranslation().z,
					  sids_[idx], trans );

    data.dataChanged();
}


void HorizonDisplay::getRandomPosCache( int channel, DataPointSet& data ) const
{
    if ( channel<0 || channel>=nrAttribs() )
       return;

    data.clearData();
    for ( int idx=0; idx<userrefs_[channel]->size(); idx++ )
	data.dataSet().add( new DataColDef(userrefs_[channel]->get(idx)) );

    for ( int idx=0; idx<sections_.size(); idx++ )
    {
	const BinIDValueSet* cache = sections_[idx]->getCache( channel );
	if ( cache )
	{
	    data.bivSet().setNrVals( cache->nrVals(), false );
	    data.bivSet().append( *cache );
	}
    }

    data.dataChanged();
}


void HorizonDisplay::updateSingleColor()
{
    const bool usesinglecol = !showsTexture();
    const Color col = usesinglecol ? nontexturecol_ : Color::White();
    material_->setColor( col );
    if ( intersectionlinematerial_ )
	intersectionlinematerial_->setColor( nontexturecol_ );

    for ( int idx=0; idx<sections_.size(); idx++ )
    {
	sections_[idx]->useChannel( !usesinglecol );
	sections_[idx]->setWireframeColor( col );
    }
}


bool HorizonDisplay::usesColor() const
{
    return !showsTexture() || displaysIntersectionLines();
}


void HorizonDisplay::setRandomPosData( int channel, const DataPointSet* data,
				       TaskRunner* trans )
{
    if ( channel<0 || channel>=nrAttribs() || sections_.isEmpty() )
       return;

    if ( !data || !data->size() )
    {
	validtexture_ = false;
	updateSingleColor();
	return;
    }

    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->setTextureData( channel, data, sids_[idx], trans );

    //We should really scale here, and then update sections. This
    //works for single sections though.
    if ( sections_[0]->getColTabMapperSetup(channel) )
    {
	coltabmappersetups_[channel] =
	    *sections_[0]->getColTabMapperSetup( channel );
	coltabmappersetups_[channel].triggerRangeChange();
    }

    validtexture_ = true;
    updateSingleColor();

    BufferStringSet* attrnms = new BufferStringSet();
    for ( int idx=0; idx<data->nrCols(); idx++ )
	attrnms->add( data->colDef(idx).name_ );
    userrefs_.replace( channel, attrnms );

    createDisplayDataPacks( channel, data );
}


void HorizonDisplay::createDisplayDataPacks(
			int channel, const DataPointSet* data )
{
    if ( !data || !data->size() || sections_.isEmpty() ) return;

    const StepInterval<int> dispinlrg = sections_[0]->displayedRowRange();
    const StepInterval<int> dispcrlrg = sections_[0]->displayedColRange();
    const BinID step( dispinlrg.step, dispcrlrg.step );
    sections_[0]->setTextureRange( dispinlrg, dispcrlrg );

    StepInterval<double> inlrg( (double)dispinlrg.start, (double)dispinlrg.stop,
				(double)dispinlrg.step );
    StepInterval<double> crlrg( (double)dispcrlrg.start, (double)dispcrlrg.stop,
				(double)dispcrlrg.step );

    const DataColDef sidcoldef( sKeySectionID() );
    const int sidcol =
	data->dataSet().findColDef(sidcoldef,PosVecDataSet::NameExact);
    const int nrfixedcols = data->nrFixedCols();
    const int shift = sidcol==-1 ?  nrfixedcols : nrfixedcols+1;
    const BinIDValueSet* cache =
	sections_.isEmpty() ? 0 : sections_[0]->getCache( channel );
    const int nrversions = cache->nrVals()-shift;

    TypeSet<DataPack::ID> dpids;
    const char* catnm = "Horizon Data";
    const char* dpnm = as_[channel]->userRef();
    BufferStringSet dimnames;
    dimnames.add("X").add("Y").add("In-line").add("Cross-line");

    for ( int idx=0; idx<nrversions; idx++ )
    {
	mDeclareAndTryAlloc(BIDValSetArrAdapter*, bvsarr,
			    BIDValSetArrAdapter(*cache,idx+shift,step));
	if ( !bvsarr ) continue;

	mDeclareAndTryAlloc(MapDataPack*,mapdp,MapDataPack(catnm,bvsarr));
	if ( !mapdp ) continue;

	mapdp->setName( dpnm );
	mapdp->setProps( inlrg, crlrg, true, &dimnames );
	DPM(DataPackMgr::FlatID()).add( mapdp );
	dpids += mapdp->id();
    }

    setDisplayDataPackIDs( channel, dpids );
}


bool HorizonDisplay::hasStoredAttrib( int channel ) const
{
    const char* userref = as_[channel]->userRef();
    return as_[channel]->id()==Attrib::SelSpec::cOtherAttrib() &&
	   userref && *userref;
}


bool HorizonDisplay::hasDepth( int channel ) const
{ return as_[channel]->id()==Attrib::SelSpec::cNoAttrib(); }


Coord3 HorizonDisplay::getTranslation() const
{
    if ( !translation_ ) return Coord3(0,0,0);

    const Coord3 current = translation_->getTranslation();

    Coord3 origin( 0, 0, 0 );
    Coord3 shift( current );
    shift  *= -1;

    mVisTrans::transformBack( transformation_, origin );
    mVisTrans::transformBack( transformation_, shift );

    const Coord3 translation = origin - shift;

    return translation;
}


void HorizonDisplay::setTranslation( const Coord3& nt )
{
     if ( !nt.isDefined() )
	return;

    Coord3 origin( 0, 0, 0 );
    Coord3 aftershift( nt );
    aftershift.z *= -1;

    mVisTrans::transform( transformation_, origin );
    mVisTrans::transform( transformation_, aftershift );

    const Coord3 shift = origin - aftershift;

    translation_->setTranslation( shift );
    translationpos_ = nt;
    setOnlyAtSectionsDisplay( displayonlyatsections_ );		/* retrigger */
}


void HorizonDisplay::setSectionDisplayRestore( bool yn )
{
    oldsectionids_.erase();
    olddisplayedrowranges_.erase();
    olddisplayedcolranges_.erase();
    deepUnRef( oldhortexhandlers_ );

    if ( yn )
    {
	for ( int idx=0; idx<sids_.size(); idx++ )
	{
	    oldsectionids_ += sids_[idx];
	    olddisplayedrowranges_ += sections_[idx]->displayedRowRange();
	    olddisplayedcolranges_ += sections_[idx]->displayedColRange();
	    oldhortexhandlers_ += &sections_[idx]->getTextureHandler();
	    oldhortexhandlers_.last()->ref();
	}
    }
}


void HorizonDisplay::removeSectionDisplay( const EM::SectionID& sid )
{
    const int idx = sids_.indexOf( sid );
    if ( idx<0 ) return;

    removeChild( sections_[idx]->osgNode() );
    sections_.removeSingle( idx )->unRef();
    secnames_.removeSingle( idx );
    sids_.removeSingle( idx );
}


bool HorizonDisplay::addSection( const EM::SectionID& sid, TaskRunner* trans )
{
    RefMan<visBase::HorizonSection> surf = visBase::HorizonSection::create();
    surf->ref();
    surf->setDisplayTransformation( transformation_ );
    surf->setZAxisTransform( zaxistransform_, trans );
    if ( scene_ ) surf->setRightHandSystem( scene_->isRightHandSystem() );

    MouseCursorChanger cursorchanger( MouseCursor::Wait );
    mDynamicCastGet( EM::Horizon3D*, horizon, emobject_ );
    surf->setSurface( horizon->geometry().sectionGeometry(sid), true, trans );
    if ( !emobject_->isEmpty() && trans && !trans->execResult() )
    {
	surf->unRef();
	return false;
    }

    surf->setResolution( resolution_-1, trans );
    surf->setMaterial( 0 );

    const int oldsecidx = oldsectionids_.indexOf( sid );
    if ( oldsecidx>=0 )
    {
	if ( surf->displayedRowRange()!=olddisplayedrowranges_[oldsecidx] ||
	     surf->displayedColRange()!=olddisplayedcolranges_[oldsecidx] )
	{
	    surf->setDisplayRange( olddisplayedrowranges_[oldsecidx],
				   olddisplayedcolranges_[oldsecidx] );
	}

	surf->setTextureHandler( *oldhortexhandlers_[oldsecidx] );
    }
    else // initialize texture handler newly created by horizon section
    {
	if ( sections_.isEmpty() && channel2rgba_ )
	{
	    surf->setChannels2RGBA( channel2rgba_ );
	    EMObjectDisplay::setChannels2RGBA( 0 );
	}

	while ( surf->nrChannels()<nrAttribs() )
	    surf->addChannel();

	for ( int idx=0; idx<nrAttribs(); idx++ )
	{
	    surf->setColTabMapperSetup( idx, coltabmappersetups_[idx], 0 );
	    surf->setColTabSequence( idx, coltabsequences_[idx] );
	    surf->getChannels2RGBA()->setEnabled( idx, enabled_[idx] );
	}

	surf->getChannels2RGBA()->allowShading( allowshading_ );
	surf->getChannels()->enableTextureInterpolation( enabletextureinterp_ );
    }

    addChild( surf->osgNode() );

    surf->turnOn( !displayonlyatsections_ );

    sections_ += surf;
    secnames_ += emobject_->name();

    sids_ += sid;
    hasmoved.trigger();

    displaysSurfaceGrid( displaysurfacegrid_ );

    return true;
}


BufferString HorizonDisplay::getSectionName( int secidx ) const
{
    if ( secidx >=secnames_.size() )
	return BufferString();

    return secnames_[secidx];
}


void HorizonDisplay::enableTextureInterpolation( bool yn )
{
    if ( enabletextureinterp_==yn )
	return;

    enabletextureinterp_ = yn;
    for ( int idx=0; idx<sections_.size(); idx++ )
    {
	if ( !sections_[idx] || !sections_[idx]->getChannels() )
	    continue;

	sections_[idx]->getChannels()->enableTextureInterpolation( yn );

    }
}


void HorizonDisplay::setOnlyAtSectionsDisplay( bool yn )
{
    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->turnOn(!yn);

    EMObjectDisplay::setOnlyAtSectionsDisplay( yn );

    displayonlyatsections_ = yn;
}


visBase::Material* HorizonDisplay::getMaterial()
{
    return material_;
}


void HorizonDisplay::setIntersectLineMaterial( visBase::Material* nm )
{
    unRefAndZeroPtr( intersectionlinematerial_ );

    intersectionlinematerial_ = nm;

    refPtr( intersectionlinematerial_ );

    for ( int idx=0; idx<intersectiondata_.size(); idx++ )
	intersectiondata_[idx]->setMaterial( intersectionlinematerial_ );
}


void HorizonDisplay::emChangeCB( CallBacker* cb )
{
    EMObjectDisplay::emChangeCB( cb );
    mCBCapsuleUnpack(const EM::EMObjectCallbackData&,cbdata,cb);
    if ( cbdata.event==EM::EMObjectCallbackData::PositionChange )
    {
	validtexture_ = false;
	const EM::SectionID sid = cbdata.pid0.sectionID();
	const int idx = sids_.indexOf( sid );
	if ( idx>=0 && idx<sections_.size() )
	    sections_[idx]->inValidateCache(-1);
    }
    else if ( cbdata.event==EM::EMObjectCallbackData::PrefColorChange )
    {
	nontexturecol_ = emobject_->preferredColor();
	setLineStyle( emobject_->preferredLineStyle() );

	mDynamicCastGet(EM::Horizon3D*,hor3d,emobject_)
	if ( hor3d )
	{
	    if ( parentline_ && parentline_->getMaterial() )
		parentline_->getMaterial()->setColor( hor3d->getParentColor() );
	    if ( selections_ && selections_->getMaterial() )
		selections_->getMaterial()->setColor(
						hor3d->getSelectionColor() );
	    if ( lockedpts_ && lockedpts_->getMaterial() )
		lockedpts_->getMaterial()->setColor( hor3d->getLockColor() );
	}
    }
    else if ( cbdata.event==EM::EMObjectCallbackData::SelectionChange )
    {
	// TODO: Should be made more general, such that it also works for
	// polygon selections
	selectChildren();
    }

    updateSingleColor();

}


int HorizonDisplay::getChannelIndex( const char* nm ) const
{
    for ( int idx=0; idx<as_.size(); idx++ )
    {
	const FixedString usrref = as_[idx]->userRef();
	if ( usrref == nm ) return idx;
    }

    return -1;
}


void HorizonDisplay::updateAuxData()
{
    const int depthidx = getChannelIndex( sKeyZValues() );
    if ( depthidx != -1 )
	setDepthAsAttrib( depthidx );

    mDynamicCastGet(EM::Horizon3D*,hor3d,emobject_)
    if ( !hor3d ) return;

    const ObjectSet<BinIDValueSet>& auxdata =
	hor3d->auxdata.getData();
    if ( auxdata.isEmpty() ) return;

    float auxvals[3];
    auxvals[0] = mUdf(float);
    auxvals[1] = hor3d->sectionID( 0 );
    for ( int idx=0; idx<auxdata[0]->nrVals(); idx++ )
    {
	const char* auxdatanm = hor3d->auxdata.auxDataName( idx );
	const int cidx = getChannelIndex( auxdatanm );
	if ( cidx==-1 ) continue;

	DataPointSet dps( false, true );
	dps.dataSet().add( new DataColDef(sKeySectionID()) );
	dps.dataSet().add( new DataColDef(auxdatanm) );

	BinIDValueSet::SPos pos;
	while ( auxdata[0]->next(pos) )
	{
	    auxvals[2] = auxdata[0]->getVal( pos, idx );
	    dps.bivSet().add( auxdata[0]->getIdxPair(pos), auxvals );
	}

	dps.dataChanged();
	setRandomPosData( cidx, &dps, 0 );
	selectTexture( cidx, 0 );
    }
}


int HorizonDisplay::nrResolutions() const
{
    return sections_.size() ? sections_[0]->nrResolutions()+1 : 1;
}


BufferString HorizonDisplay::getResolutionName( int res ) const
{
    BufferString str;
    if ( !res ) str = "Automatic";
    else
    {
	res--;
	int val = 1;
	for ( int idx=0; idx<res; idx++ )
	    val *= 2;

	if ( val==2 )		str = "Half";
	else if ( val==1 )	str = "Full";
	else			{ str = "1 / "; str += val; }
    }

    return str;
}


int HorizonDisplay::getResolution() const
{
    return sections_.size() ? sections_[0]->currentResolution()+1 : 0;
}


bool HorizonDisplay::displaysSurfaceGrid() const
{
    if ( sections_.size() )
       return  sections_[0]->isWireframeDisplayed();
    return false;
}

void HorizonDisplay::setResolution( int res, TaskRunner* trans )
{
    resolution_ = (char)res;
    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->setResolution( resolution_-1, trans );
}


void HorizonDisplay::displaysSurfaceGrid( bool yn )
{
    displaysurfacegrid_ = yn;
    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->enableGeometryTypeDisplay( WireFrame, yn );
    requestSingleRedraw();
}

const ColTab::Sequence* HorizonDisplay::getColTabSequence( int channel ) const
{
    if ( channel<0 || channel>=nrAttribs() )
       return 0;

    return sections_.size()
	? sections_[0]->getColTabSequence( channel )
	: &coltabsequences_[channel];
}


bool HorizonDisplay::canSetColTabSequence() const
{
    if ( !usesTexture() || !sections_.size() ||
	 !sections_[0]->getChannels2RGBA() )
	return false;

    return sections_[0]->getChannels2RGBA()->canSetSequence();
}


void HorizonDisplay::setColTabSequence( int chan, const ColTab::Sequence& seq,
					TaskRunner* )
{
    if ( chan<0 || chan>=nrAttribs() )
       return;

    coltabsequences_[chan] = seq;

    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->setColTabSequence( chan, seq );
}


void HorizonDisplay::setColTabMapperSetup( int channel,
			   const ColTab::MapperSetup& ms, TaskRunner* trans )
{
    if ( channel<0 || channel>=nrAttribs() )
       return;

    if ( coltabmappersetups_.validIdx(channel) )
	coltabmappersetups_[channel] = ms;

    for ( int idx=0; idx<sections_.size(); idx++ )
	sections_[idx]->setColTabMapperSetup( channel, ms, trans );

    //We should really scale here, and then update sections. This
    //works for single sections though.
    if ( sections_.size() && sections_[0]->getColTabMapperSetup( channel ) )
    {
	coltabmappersetups_[channel] =
	    *sections_[0]->getColTabMapperSetup( channel );
    }
}


const ColTab::MapperSetup* HorizonDisplay::getColTabMapperSetup( int ch,
								 int ) const
{
    if ( ch<0 || ch>=nrAttribs() )
       return 0;

    return &coltabmappersetups_[ch];
}


const TypeSet<float>* HorizonDisplay::getHistogram( int attrib ) const
{
    if ( !sections_.size() )
	return 0;

    return sections_[0]->getHistogram( attrib );
}


float HorizonDisplay::calcDist( const Coord3& pickpos ) const
{
    if ( !emobject_ ) return mUdf(float);

    const mVisTrans* utm2display = scene_->getUTM2DisplayTransform();
    Coord3 xytpos;
    utm2display->transformBack( pickpos, xytpos );
    mDynamicCastGet(const EM::Horizon3D*,hor,emobject_)
    if ( hor )
    {
	const BinID bid = SI().transform( xytpos );
	const EM::SubID bidid = bid.toInt64();
	TypeSet<Coord3> positions;
	for ( int idx=sids_.size()-1; idx>=0; idx-- )
	{
	    if ( !emobject_->isDefined( sids_[idx], bidid ) )
		continue;

	    positions += emobject_->getPos( sids_[idx], bidid );
	}

	float mindist = mUdf(float);
	for ( int idx=0; idx<positions.size(); idx++ )
	{
	    const float zfactor = scene_
		? scene_->getZScale()
		: s3dgeom_->zScale();
	    const Coord3& pos = positions[idx] + getTranslation()/zfactor;
	    const float dist = (float) fabs(xytpos.z-pos.z);
	    if ( dist < mindist ) mindist = dist;
	}

	return mindist;
    }

    return mUdf(float);
}


float HorizonDisplay::maxDist() const
{
    return s3dgeom_->zStep();
}


visBase::HorizonSection* HorizonDisplay::getHorizonSection(
	const EM::SectionID& sid )
{
    for ( int idx=0; idx<sids_.size(); idx++ )
    {
	if ( sids_[idx]==sid )
	    return sections_[idx];
    }

    return 0;
}


EM::SectionID HorizonDisplay::getSectionID( int visid ) const
{
    for ( int idx=0; idx<sections_.size(); idx++ )
    {
	if ( sections_[idx]->id()==visid )
		return sids_[idx];
    }

    return -1;
}


void HorizonDisplay::getMousePosInfo( const visBase::EventInfo& eventinfo,
				       Coord3& pos, BufferString& val,
				       BufferString& info ) const
{
    EMObjectDisplay::getMousePosInfo( eventinfo, pos, val, info );
    if ( !emobject_ || !showsTexture() ) return;

    const EM::SectionID sid =
	EMObjectDisplay::getSectionID(&eventinfo.pickedobjids);

    const int sectionidx = sids_.indexOf( sid );
    if ( sectionidx<0 || sectionidx>=sections_.size() )
	return;

    const BinID bid( SI().transform(pos) );

    for ( int idx=as_.size()-1; idx>=0; idx-- )
    {
	if ( as_[idx]->id().isUnselInvalid() )
	    return;

	if ( !sections_[sectionidx]->getChannels2RGBA()->isEnabled(idx) ||
	      sections_[sectionidx]->getTransparency(idx)==255 )
	    continue;

	const BinIDValueSet* bidvalset = sections_[sectionidx]->getCache( idx );
	if ( !bidvalset || bidvalset->nrVals()<2 ) continue;

	const BinIDValueSet::SPos setpos = bidvalset->find( bid );
	if ( !setpos.isValid() )
	    continue;

	const float* vals = bidvalset->getVals( setpos );
	int curtexture = selectedTexture(idx);
	if ( curtexture>bidvalset->nrVals()-2 ) curtexture = 0;

	const float fval = vals[curtexture+2];
	bool islowest = true;
	for ( int idy=idx-1; idy>=0; idy-- )
	{
	    if ( !sections_[sectionidx]->getCache(idy) ||
		 !isAttribEnabled(idy) ||
		 sections_[sectionidx]->getTransparency(idy)==255 )
		continue;

		islowest = false;
		break;
	}

	if ( !islowest )
	{
	    const ColTab::Sequence* coltabseq = getColTabSequence( idx );
	    const Color col = coltabseq ? coltabseq->color(fval) : Color();
	    if ( (!coltabseq || col!=coltabseq->undefColor()) && col.t()==255 )
		continue;
	}

	if ( !mIsUdf(fval) )
	{
	    val = fval;
	    float attribshift = 0;
	    const TypeSet<float>& attribshifts = *shifts_[idx];
	    const int version = selectedTexture( idx );
	    if ( attribshifts.validIdx(version) )
	    {
		attribshift =
		  attribshifts[version] * s3dgeom_->zDomain().userFactor();
	    }

	    const float zshift =
	      (float) getTranslation().z*s3dgeom_->zDomain().userFactor();

	    const bool hasshift = !mIsZero(attribshift,0.1) ||
				  !mIsZero(zshift,0.1);
	    if ( as_.size() > 1 || hasshift )
	    {
		BufferString channelstr = "(";
		bool first = true;
		if ( as_.size()>1 )
		{
		    channelstr += as_[idx]->userRef();
		    first = false;
		}

		if ( hasshift )
		{
		    if ( !first )
			channelstr += ", ";
		    channelstr += "shift=";
		    channelstr += attribshift;
		}

		channelstr += ")";
		val.replaceAt( cValNameOffset(), channelstr );
	    }
	}

	return;
    }
}


void HorizonDisplay::traverseLine( const TrcKeyPath& path,
				   const Interval<float>& zrg,
                                   EM::SectionID sid,
                                   HorizonDisplay::IntersectionData& res ) const
{
    mDynamicCastGet( EM::Horizon3D*, hor, emobject_ );
    //Remove everything

    if ( !path.size() )
        return;

    const TypeSet<EM::PosID>* seedposids = hor->getPosAttribList
						( EM::EMObject::sSeedNode() );

    const Pos::GeomID pathgeomid = path[0].geomID();
    const Pos::GeomID horgeomid = hor->getSurveyGeomID();
    const Survey::Geometry* pathgeom = Survey::GM().getGeometry( pathgeomid );
    const Survey::Geometry* horgeom = Survey::GM().getGeometry( horgeomid );
    if ( !pathgeom || !horgeom )
        return;

    TypeSet<Coord3> curline;
    for ( int idx=0; idx<path.size(); idx++ )
    {
        TrcKey hortrc = TrcKey::udf();
        if ( pathgeomid==horgeomid ) //We are on the same grid
        {
            hortrc = path[idx];
        }
        else
        {
            const Coord pathpos = pathgeom->toCoord(path[idx]);
            hortrc = horgeom->getTrace( pathpos, horgeom->averageTrcDist() );
        }

        EM::SubID horsubid = hortrc.isUdf()
                ? mUdf(EM::SubID)
                : hortrc.pos().toInt64();

        if ( seedposids && !mIsUdf(horsubid) &&
             seedposids->isPresent(EM::PosID(hor->id(),sid,horsubid)))
        {
            horsubid = mUdf(EM::SubID);
        }

        if ( !mIsUdf(horsubid) )
        {
	    Coord3 horpos = hor->getPos(sid,horsubid);
	    if ( horpos.isDefined() )
	    {
		if ( zaxistransform_ )
		    horpos.z = zaxistransform_->transformTrc( hortrc,
                                                  	  (float) horpos.z );
	    }

	    if ( horpos.isDefined() && zrg.includes(horpos.z,false) )
	    {
		curline += horpos;
		continue;
	    }
        }

	res.addLine( curline );
	curline.erase();
    }

    res.addLine( curline );
}

#define mAddLinePrimitiveSet \
{\
    if ( idxps.size()>=2 )\
    {\
       Geometry::IndexedPrimitiveSet* primitiveset =  \
               Geometry::IndexedPrimitiveSet::create( false );\
       primitiveset->ref();\
       primitiveset->append( idxps.arr(), idxps.size() );\
       line->addPrimitiveSet( primitiveset ); \
       primitiveset->unRef();\
    }\
}



void HorizonDisplay::drawHorizonOnZSlice( const TrcKeyZSampling& tkzs,
			const EM::SectionID&  sid,
			HorizonDisplay::IntersectionData& res ) const
{
    mDynamicCastGet(const EM::Horizon3D*,horizon,emobject_);
    if ( !horizon ) return;

    const float zshift = (float) getTranslation().z;

    const Geometry::BinIDSurface* geom =
	horizon->geometry().sectionGeometry( sid );

    const Array2D<float>* field = geom->getArray();
    if ( !field ) return;

    ConstPtrMan<Array2D<float> > myfield;
    if ( zaxistransform_ )
    {
	myfield = field = horizon->createArray2D( sid, zaxistransform_ );
    }

    IsoContourTracer ictracer( *field );
    ictracer.setSampling( geom->rowRange(), geom->colRange() );
    ictracer.selectRectROI( tkzs.hsamp_.inlRange(), tkzs.hsamp_.crlRange() );
    ManagedObjectSet<ODPolygon<float> > isocontours;
    ictracer.getContours( isocontours, tkzs.zsamp_.start-zshift, false );

    TypeSet<int> idxps;
    for ( int cidx=0; cidx<isocontours.size(); cidx++ )
    {
	const ODPolygon<float>& ic = *isocontours[cidx];
	TypeSet<Coord3> curline;

	for ( int vidx=0; vidx<ic.size(); vidx++ )
	{
	    const Geom::Point2D<float> vertex = ic.getVertex( vidx );
	    Coord vrtxcoord( vertex.x, vertex.y );
	    vrtxcoord = SI().binID2Coord().transform( vrtxcoord );
	    const Coord3 pos( vrtxcoord, tkzs.zsamp_.start-zshift );
	    curline += pos;
	}

	if ( ic.isClosed() && curline.size() )
	    curline += curline[0];

	res.addLine( curline );
    }
}


HorizonDisplay::IntersectionData*
HorizonDisplay::getOrCreateIntersectionData(
		ObjectSet<IntersectionData>& pool )
{
    IntersectionData* data = 0;
    if ( pool.size() )
    {
	data = pool.pop();
	data->clear();
    }
    else
    {
	data = new IntersectionData( *lineStyle() );
	data->setDisplayTransformation(transformation_);
	if ( intersectionlinematerial_ )
	    data->setMaterial( intersectionlinematerial_ );
	addChild( data->line_->osgNode() );
	addChild( data->markerset_->osgNode() );
    }

    return data;
}


void HorizonDisplay::updateIntersectionLines(
	    const ObjectSet<const SurveyObject>& objs, int whichobj )
{
    mDynamicCastGet(const EM::Horizon3D*,horizon,emobject_);
    if ( !horizon ) return;

    ManagedObjectSet<IntersectionData> lines;
    while ( intersectiondata_.size() )
	lines += intersectiondata_.removeAndTake(0,false);

    if ( displayonlyatsections_ || displayintersectionlines_ )
    {
	for ( int idx=0; idx<objs.size(); idx++ )
	{
            const TrcKeyZSampling trzs = objs[idx]->getTrcKeyZSampling(-1);

            TrcKeyPath path;
            objs[idx]->getTraceKeyPath( path );
	    const Interval<float> zrg = objs[idx]->getDataTraceRange();

            if ( path.isEmpty() && trzs.isEmpty() )
                continue;

	    IntersectionData* data = 0;

	    for ( int sectionidx=0; sectionidx<horizon->nrSections();
		  sectionidx++ )
	    {
		const EM::SectionID sid = horizon->sectionID(sectionidx);
		if ( path.size() )
		{
		    data = getOrCreateIntersectionData( lines );
		    traverseLine( path, zrg, sid, *data );
            	    continue;
		}
		else
		{
		    if ( mIsZero(zrg.width(),1e-5) )
		    {
			data = getOrCreateIntersectionData( lines );
			drawHorizonOnZSlice( trzs, sid, *data );
		    }
		}
	    }

	    intersectiondata_ += data;
	}
    }

    //These lines were not used, hance remove from scene.
    for ( int idx=0; idx<lines.size(); idx++ )
    {
	removeChild( lines[idx]->line_->osgNode() );
	removeChild( lines[idx]->markerset_->osgNode() );
    }
}


void HorizonDisplay::setLineStyle( const OD::LineStyle& lst )
{
    if ( lst==*lineStyle() )
	return;

    EMObjectDisplay::setLineStyle( lst );

    for ( int idx=0; idx<intersectiondata_.size(); idx++ )
    {
	RefMan<visBase::VertexShape> old =
	    intersectiondata_[idx]->setLineStyle( lst );
	if ( old )
	{
	    removeChild( old->osgNode() );
	    addChild( intersectiondata_[idx]->line_->osgNode());
	}
    }
}


void HorizonDisplay::updateSectionSeeds(
	    const ObjectSet<const SurveyObject>& objs, int movedobj )
{
    bool refresh = movedobj==-1 || movedobj==id();
    TypeSet<int> planelist;

    for ( int idx=0; idx<objs.size(); idx++ )
    {
	mDynamicCastGet(const PlaneDataDisplay*,plane,objs[idx]);
	if ( plane && plane->getOrientation()!=OD::ZSlice )
	{
	    planelist += idx;
	    if ( movedobj==plane->id() )
		refresh = true;
	}

	mDynamicCastGet( const MPEDisplay*, mped, objs[idx] );
	if ( mped && mped->isDraggerShown() )
	{
	    TrcKeyZSampling tkzs;
	    if ( mped->getPlanePosition(tkzs) && tkzs.nrZ()!=1 )
	    {
		planelist += idx;
		if ( movedobj==mped->id() )
		    refresh = true;
	    }
	}
    }

    if ( !refresh ) return;

    for ( int idx=0; idx<posattribmarkers_.size(); idx++ )
    {
	visBase::MarkerSet* markerset = posattribmarkers_[idx];
	for ( int idy=0; idy<markerset->getCoordinates()->size(); idy++ )
	{
	    markerset->turnMarkerOn( idy,!displayonlyatsections_ );
	    const visBase::Coordinates* markercoords =
					markerset->getCoordinates();
	    if ( markercoords->size() )
	    {
		Coord3 markerpos = markercoords->getPos( idy );
		if ( !markerpos.isDefined() )
		{
		    markerset->turnMarkerOn( idy, false );
		    continue;
		}

		if ( transformation_ )
		     mVisTrans::transform( transformation_,  markerpos );

		if ( zaxistransform_ )
		{
		    markerset->turnMarkerOn( idy,false );
		    continue;
		}

		for ( int idz=0; idz<planelist.size(); idz++ )
		{
		    const float dist =
			objs[planelist[idz]]->calcDist( markerpos );
		    if ( dist < objs[planelist[idz]]->maxDist() )
		    {
			markerset->turnMarkerOn( idy,true );
			break;
		    }
		}
	    }
	}
    }
}


void HorizonDisplay::selectParent( const TrcKey& tk )
{
    mDynamicCastGet(const EM::Horizon3D*,hor3d,emobject_)
    if ( !hor3d ) return;

    if ( !parentline_ )
    {
	parentline_ = visBase::PolyLine3D::create();
	parentline_->setMaterial( new visBase::Material() );
	parentline_->getMaterial()->setColor( hor3d->getParentColor() );
	addChild( parentline_->osgNode() );
	parentline_->setDisplayTransformation( transformation_ );
    }
    else
    {
	parentline_->getCoordinates()->setEmpty();
	parentline_->removeAllPrimitiveSets();
    }

    TypeSet<int> idxps;
    int cii = 0;
    TypeSet<TrcKey> parents;
    hor3d->getParents( tk, parents );
    for ( int idx=0; idx<parents.size(); idx++ )
    {
	const TrcKey& curnode = parents[idx];
	const Coord3 crd( SI().transform(curnode.pos()), hor3d->getZ(curnode) );
	parentline_->getCoordinates()->addPos( crd );
	idxps.add( cii++ );
    }

    visBase::VertexShape* line = parentline_;
    mAddLinePrimitiveSet;
    showParentLine( true );
}


void HorizonDisplay::initSelectionDisplay( bool erase )
{
    mDynamicCastGet(const EM::Horizon3D*,hor3d,emobject_)
    if ( !selections_ )
    {
	selections_ = visBase::PointSet::create();
	selections_->ref();
	if ( hor3d && selections_->getMaterial() )
	    selections_->getMaterial()->setColor( hor3d->getSelectionColor() );
	addChild( selections_->osgNode() );
	selections_->setDisplayTransformation( transformation_ );
    }
    else if ( erase )
    {
	selections_->removeAllPoints();
	selections_->removeAllPrimitiveSets();
	selections_->getMaterial()->clear();
    }
}


void HorizonDisplay::selectChildren()
{
    mDynamicCastGet(const EM::Horizon3D*,hor3d,emobject_)
    Array2D<char>* children = hor3d ? hor3d->getChildren() : 0;
    if ( !children ) return;

    initSelectionDisplay( true );

    const TrcKeySampling tks = hor3d->getTrackingSampling();
    const EM::SectionID sid = hor3d->sectionID( 0 );
    TypeSet<int> pidxs;
    for ( od_int64 gidx=0; gidx<children->info().getTotalSz(); gidx++ )
    {
	if ( children->getData()[gidx] == '0' )
	    continue;

	const TrcKey tk = tks.atIndex( gidx );
	const Coord3 pos = hor3d->getPos( sid, tk.pos().toInt64() );
	const int pidx = selections_->addPoint( pos );
	selections_->getMaterial()->setColor( hor3d->getSelectionColor(), pidx);
	pidxs += pidx;
    }

    Geometry::PrimitiveSet* pointsetps =
		Geometry::IndexedPrimitiveSet::create( true );
    pointsetps->setPrimitiveType( Geometry::PrimitiveSet::Points );
    pointsetps->append( pidxs.arr(), pidxs.size() );
    selections_->addPrimitiveSet( pointsetps );
    selections_->turnOn( true );
}


void HorizonDisplay::showParentLine( bool yn )
{
    if ( parentline_ ) parentline_->turnOn( yn );
}


void HorizonDisplay::showSelections( bool yn )
{
    if ( selections_ ) selections_->turnOn( yn );
}


void HorizonDisplay::showLocked( bool yn )
{
    mDynamicCastGet(const EM::Horizon3D*,hor3d,emobject_)
    const Array2D<char>* locked = hor3d ? hor3d->getLockedNodes() : 0;
    if ( !locked ) return;

    if ( !lockedpts_ && yn )
    {
	lockedpts_ = visBase::PointSet::create();
	lockedpts_->ref();
	addChild( lockedpts_->osgNode() );
	lockedpts_->setDisplayTransformation( transformation_ );
    }

    if ( lockedpts_ )
    {
	lockedpts_->removeAllPoints();
	lockedpts_->removeAllPrimitiveSets();
	lockedpts_->getMaterial()->clear();
	if ( lockedpts_->getMaterial() )
	    lockedpts_->getMaterial()->setColor( hor3d->getLockColor() );
    }

    if ( !yn )
    {
	lockedpts_->turnOn( false );
	return;
    }

    const TrcKeySampling tks = hor3d->getTrackingSampling();
    const EM::SectionID sid = hor3d->sectionID( 0 );
    TypeSet<int> pidxs;
    for ( od_int64 gidx=0; gidx<locked->info().getTotalSz(); gidx++ )
    {
	if ( locked->getData()[gidx] == '0' )
	    continue;

	const TrcKey tk = tks.atIndex( gidx );
	const Coord3 pos = hor3d->getPos( sid, tk.pos().toInt64() );
	const int pidx = lockedpts_->addPoint( pos );
	pidxs += pidx;
    }

    Geometry::PrimitiveSet* pointsetps =
		Geometry::IndexedPrimitiveSet::create( true );
    pointsetps->setPrimitiveType( Geometry::PrimitiveSet::Points );
    pointsetps->append( pidxs.arr(), pidxs.size() );
    lockedpts_->addPrimitiveSet( pointsetps );
    lockedpts_->turnOn( true );
}


bool HorizonDisplay::lockedShown() const
{ return lockedpts_ ? lockedpts_->isOn() : false; }


void HorizonDisplay::updateSelections()
{
    EMObjectDisplay::updateSelections();
    const int lastidx = selectors_.size() - 1;
    if ( lastidx<0 ) return;

    mDynamicCastGet(const EM::Horizon3D*,hor3d,emobject_)
    if ( !hor3d ) return;

    initSelectionDisplay( lastidx==0 );

    const EM::SectionID sid = hor3d->sectionID(0);
    const Selector<Coord3>* sel = selectors_[lastidx];
    ObjectSet<const Selector<Coord3> > selectors;
    selectors += sel;
    EM::EMObjectPosSelector posselector( *hor3d, sid, selectors );
    posselector.execute();

    TypeSet<int> pidxs;
    const TypeSet<EM::SubID>& selids = posselector.getSelected();
    if ( selids.size()<=0 )
	return;

    for ( int idx=0; idx<selids.size(); idx++ )
    {
    	const Coord3 pos = hor3d->getPos( sid, selids[idx] );
    	if ( pos.isUdf() ) continue;

	const int pidx = selections_->addPoint( pos );
	selections_->getMaterial()->setColor( hor3d->getSelectionColor(), pidx);
	pidxs += pidx;
    }

    Geometry::PrimitiveSet* pointsetps =
		Geometry::IndexedPrimitiveSet::create( true );
    pointsetps->setPrimitiveType( Geometry::PrimitiveSet::Points );
    pointsetps->append( pidxs.arr(), pidxs.size() );
    selections_->addPrimitiveSet( pointsetps );
    selections_->turnOn( true );
}


void HorizonDisplay::clearSelections()
{
    EMObjectDisplay::clearSelections();
    if ( selections_ )
    {
	selections_->removeAllPoints();
	selections_->removeAllPrimitiveSets();
	selections_->getMaterial()->clear();
    }
}


void HorizonDisplay::doOtherObjectsMoved(
	const ObjectSet<const SurveyObject>& objs, int whichobj )
{
    otherObjectsMoved( objs, whichobj );
}


void HorizonDisplay::otherObjectsMoved(
	    const ObjectSet<const SurveyObject>& objs, int whichobj )
{
    updateIntersectionLines( objs, whichobj );
    updateSectionSeeds( objs, whichobj );
}


void HorizonDisplay::fillPar( IOPar& par ) const
{
    EMObjectDisplay::fillPar( par );

    if ( emobject_ && !emobject_->isFullyLoaded() )
    {
	par.set( sKeyRowRange(), geometryRowRange() );
	par.set( sKeyColRange(), geometryColRange() );
    }

    par.setYN( sKeyTexture(), usesTexture() );
    par.set( sKeyShift(), getTranslation().z );
    par.set( sKeyResolution(), getResolution() );
    par.set( sKeySurfaceGrid(), displaysSurfaceGrid() );

    for ( int channel=as_.size()-1; channel>=0; channel-- )
    {
	IOPar channelpar;
	as_[channel]->fillPar( channelpar );

	getColTabMapperSetup(channel)->fillPar( channelpar );
	if ( getColTabSequence(channel) )
	    getColTabSequence(channel)->fillPar( channelpar );

	channelpar.setYN( sKeyIsOn(), isAttribEnabled(channel) );

	BufferString key = sKeyAttribs();
	key += channel;
	par.mergeComp( channelpar, key );
    }

    par.set( sKeyNrAttribs(), as_.size() );
}


bool HorizonDisplay::usePar( const IOPar& par )
{
    if ( !EMObjectDisplay::usePar( par ) )
	 return false;

    if ( scene_ )
	setDisplayTransformation( scene_->getUTM2DisplayTransform() );

    if ( !par.get(sKeyEarthModelID(),parmid_) )
	return false;

    par.get( sKeyRowRange(), parrowrg_ );
    par.get( sKeyColRange(), parcolrg_ );

    if ( !par.getYN(sKeyTexture(),usestexture_) )
	usestexture_ = true;

    int resolution = 0;
    par.get( sKeyResolution(), resolution );
    setResolution( resolution, 0 );

    Coord3 shift( 0, 0, 0 );
    par.get( sKeyShift(), shift.z );
    setTranslation( shift );

    bool displaysurfacegrid =  false;
    par.getYN( sKeySurfaceGrid(), displaysurfacegrid );
    displaysSurfaceGrid( displaysurfacegrid );

    int intersectlinematid;
    if ( par.get(sKeyIntersectLineMaterialID(),intersectlinematid) )
    {
	if ( intersectlinematid==-1 )
	    setIntersectLineMaterial( 0 );
	else
	{
	    DataObject* mat = visBase::DM().getObject( intersectlinematid );
	    if ( !mat ) return 0;
	    if ( typeid(*mat) != typeid(visBase::Material) ) return -1;

	    setIntersectLineMaterial( (visBase::Material*)mat );
	}
    }

    return true;
}


void HorizonDisplay::setDisplayDataPackIDs( int attrib,
				const TypeSet<DataPack::ID>& newdpids )
{
    TypeSet<DataPack::ID>& dpids = *dispdatapackids_[attrib];
    for ( int idx=dpids.size()-1; idx>=0; idx-- )
	DPM(DataPackMgr::FlatID()).release( dpids[idx] );

    dpids = newdpids;
    for ( int idx=dpids.size()-1; idx>=0; idx-- )
	DPM(DataPackMgr::FlatID()).obtain( dpids[idx] );
}


DataPack::ID HorizonDisplay::getDataPackID( int channel ) const
{
    return getDisplayedDataPackID( channel );
}


DataPack::ID HorizonDisplay::getDisplayedDataPackID( int channel ) const
{
    if ( sections_.isEmpty() || !dispdatapackids_.validIdx(channel) )
	return DataPack::cNoID();

    const TypeSet<DataPack::ID>& dpids = *dispdatapackids_[channel];
    const int curversion = sections_[0]->activeVersion( channel );
    return dpids.validIdx(curversion) ? dpids[curversion] : DataPack::cNoID();
}


const visBase::HorizonSection* HorizonDisplay::getSection( int horsecid ) const
{
    return sections_.validIdx( horsecid ) ? sections_[horsecid] : 0;
}


HorizonDisplay* HorizonDisplay::getHorizonDisplay( const MultiID& mid )
{
    TypeSet<int> ids;
    visBase::DM().getIDs( typeid(visSurvey::HorizonDisplay), ids );

    for ( int idx=0; idx<ids.size(); idx++ )
    {
	DataObject* dataobj = visBase::DM().getObject( ids[idx] );
	mDynamicCastGet( HorizonDisplay*, hordisp, dataobj );
	if ( hordisp && mid==hordisp->getMultiID() )
	    return hordisp;
    }
    return 0;
}


void HorizonDisplay::setPixelDensity( float dpi )
{
    EMObjectDisplay::setPixelDensity( dpi );

    for ( int idx=0; idx<intersectiondata_.size(); idx++ )
	intersectiondata_[idx]->setPixelDensity( dpi );
}


HorizonDisplay::IntersectionData::IntersectionData( const OD::LineStyle& lst )
    : line_( lst.type_==OD::LineStyle::Solid
	? (visBase::VertexShape*) visBase::PolyLine3D::create()
	: (visBase::VertexShape*) visBase::PolyLine::create() )
    , markerset_( visBase::MarkerSet::create() )
    , voiid_(-2)
    , zaxistransform_(0)
{
    line_->ref();
    markerset_->ref();

    line_->setPrimitiveType( Geometry::PrimitiveSet::LineStrips );

    setLineStyle( lst );
}


HorizonDisplay::IntersectionData::~IntersectionData()
{
    if ( zaxistransform_ )
    {
        zaxistransform_->removeVolumeOfInterest( voiid_ );
        unRefAndZeroPtr( zaxistransform_ );
    }

    unRefAndZeroPtr( line_ );
    unRefAndZeroPtr( markerset_ );
}


void HorizonDisplay::IntersectionData::addLine( const TypeSet<Coord3>& crds )
{
    if ( !crds.size() )
        return;

    if ( crds.size()==1 )
    {
        markerset_->addPos( crds[0] );
        return;
    }

    const int start = line_->getCoordinates()->size();
    const int stop = start + crds.size()-1;

    line_->getCoordinates()->setPositions( crds.arr(), crds.size(), start );

    Geometry::RangePrimitiveSet* rps = Geometry::RangePrimitiveSet::create();
    rps->setRange( Interval<int>( start, stop ) );
    line_->addPrimitiveSet( rps );
}


void HorizonDisplay::IntersectionData::clear()
{
    line_->removeAllPrimitiveSets();
    line_->getCoordinates()->setEmpty();
    markerset_->clearMarkers();
}


void HorizonDisplay::IntersectionData::setDisplayTransformation(
						const mVisTrans* tran)
{
    line_->setDisplayTransformation( tran );
    markerset_->setDisplayTransformation( tran );
}


void HorizonDisplay::IntersectionData::updateDataTransform(
        const TrcKeyZSampling& sampling, ZAxisTransform* trans )
{
    if ( zaxistransform_ && zaxistransform_!=trans )
    {
        zaxistransform_->removeVolumeOfInterest( voiid_ );
        unRefAndZeroPtr( zaxistransform_ );
        voiid_ = -2;
    }
    zaxistransform_ = trans;
    if ( zaxistransform_ )
    {
	if ( voiid_==-2 )
	    voiid_ = zaxistransform_->addVolumeOfInterest(sampling,true);
	else
	    zaxistransform_->setVolumeOfInterest( voiid_,sampling,true);

	if ( voiid_>=0 )
	    zaxistransform_->loadDataIfMissing( voiid_ );
    }
}


void HorizonDisplay::IntersectionData::setPixelDensity( float dpi )
{
    if ( line_ ) line_->setPixelDensity( dpi );
    if ( markerset_ ) markerset_->setPixelDensity( dpi );
}


void HorizonDisplay::IntersectionData::setMaterial( visBase::Material* mat )
{
    if ( line_ ) line_->setMaterial( mat );
    if ( markerset_ ) markerset_->setMaterial( mat );
}


void HorizonDisplay::IntersectionData::setSceneEventCatcher(
						visBase::EventCatcher* ev)
{
    if ( line_ ) line_->setSceneEventCatcher( ev );
    if ( markerset_ ) markerset_->setSceneEventCatcher( ev );
}


RefMan<visBase::VertexShape>
HorizonDisplay::IntersectionData::setLineStyle( const OD::LineStyle& lst )
{
    RefMan<visBase::VertexShape> oldline = 0;
    if ( line_ )
    {
	mDynamicCastGet( visBase::PolyLine3D*,ln3d,line_);

	const bool removelines =
	    (lst.type_==OD::LineStyle::Solid) != ((bool) ln3d );

	if ( removelines )
	{
	    visBase::VertexShape* newline = lst.type_==OD::LineStyle::Solid
		? (visBase::VertexShape*) visBase::PolyLine3D::create()
		: (visBase::VertexShape*) visBase::PolyLine::create();
	    newline->ref();
	    newline->setRightHandSystem( line_->isRightHandSystem() );
	    newline->setDisplayTransformation(
		    			line_->getDisplayTransformation());
	    newline->getCoordinates()->copyFrom( *line_->getCoordinates() );
	    if ( line_->getMaterial() )
		newline->setMaterial( line_->getMaterial() );

	    oldline = line_;
	    unRefAndZeroPtr( line_ );
	    line_ = newline;
	    line_->ref();
	}

	if ( lst.type_==OD::LineStyle::Solid )
	{
	    ((visBase::PolyLine3D* ) line_ )->setLineStyle( lst );
	}
    }

    markerset_->setScreenSize(((float) lst.width_)/2.0f);

    return oldline;
}
