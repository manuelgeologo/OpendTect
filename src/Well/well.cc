/*+
 * (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 * AUTHOR   : A.H. Bril
 * DATE     : Aug 2003
-*/

static const char* rcsID mUsedVar = "$Id$";

#include "welldata.h"
#include "welltrack.h"
#include "welllog.h"
#include "welllogset.h"
#include "welldisp.h"
#include "welld2tmodel.h"
#include "wellmarker.h"
#include "bendpointfinder.h"
#include "idxable.h"
#include "iopar.h"
#include "stratlevel.h"
#include "wellman.h"

const char* Well::Info::sKeyuwid()	{ return "Unique Well ID"; }
const char* Well::Info::sKeyoper()	{ return "Operator"; }
const char* Well::Info::sKeystate()	{ return "State"; }
const char* Well::Info::sKeycounty()	{ return "County"; }
const char* Well::Info::sKeycoord()	{ return "Surface coordinate"; }
const char* Well::Info::sKeykbelev(){ return "Reference Datum elevation [KB]"; }
const char* Well::Info::sKeyreplvel()
{ return "Replacement velocity [from KB to SRD]"; }
const char* Well::Info::sKeygroundelev(){ return "Ground Level elevation [GL]";}
const char* Well::Info::sKeyTD()	{ return "Total Depth [TD]"; }
const char* Well::Info::sKeywelltype()	{ return "WellType"; }

DefineEnumNames( Well::Info, WellType, 0, "Well Type" )
{ "none", "oilwell", "gaswell", "oilgaswell", "dryhole", "pluggedoilwell",
  "pluggedgaswell", "pluggedoilgaswell", "permittedlocation",
  "canceledlocation", "injectiondisposalwell", 0 };

int Well::Info::legacyLogWidthFactor()
{
   const int inlnr = SI().inlRange( true ).nrSteps() + 1;
   const int crlnr = SI().crlRange( true ).nrSteps() + 1;
   const float survfac = Math::Sqrt( (float)(crlnr*crlnr + inlnr*inlnr) );
   const int legacylogwidthfac = mNINT32(survfac*43/1000);
   //hack 43 best factor based on F3_Demo
   return legacylogwidthfac!=0 ? legacylogwidthfac : 1;
}


int Well::DahObj::indexOf( float dh ) const
{
    int idx1 = -1;
    IdxAble::findFPPos( dah_, dah_.size(), dh, -1, idx1 );
    return idx1;
}


Interval<float>	Well::DahObj::dahRange() const
{
    if ( isEmpty() )
	return Interval<float>( 0, 0 );

    return Interval<float>( dah_.first(), dah_.last() );
}


float Well::DahObj::dahStep( bool ismin ) const
{
    const int sz = dah_.size();
    if ( sz < 2 ) return mUdf(float);

    float res = dah_[1] - dah_[0];
    if ( res <0 ) res = 0;
    int nrvals = 1;
    for ( int idx=2; idx<sz; idx++ )
    {
	float val = dah_[idx] - dah_[idx-1];
	if ( mIsZero(val,mDefEps) )
	    continue;

	if ( !ismin )
	    res += val;
	else
	{
	    if ( val < res && val >= 0 )
		res = val;
	}
	nrvals++;
    }

    if ( !ismin ) res /= nrvals; // average
    return mIsZero(res,mDefEps) ? mUdf(float) : res;
}


void Well::DahObj::deInterpolate()
{
    TypeSet<Coord> bpfinp;
    bpfinp.setCapacity( dah_.size(), false );
    for ( int idx=0; idx<dah_.size(); idx++ )
	bpfinp += Coord( dah_[idx]*0.1, value( idx ) );
	// for time we want a fac of 1000, but for track 1. Compromise.

    BendPointFinder2D finder( bpfinp, 1e-5 );
    if ( !finder.execute() || finder.bendPoints().size()<1 )
	return;

    const TypeSet<int>& bpidxs = finder.bendPoints();

    int bpidx = 0;
    TypeSet<int> torem;
    for ( int idx=0; idx<bpfinp.size(); idx++ )
    {
	if ( idx != bpidxs[bpidx] )
	    torem += idx;
	else
	    bpidx++;
    }

    for ( int idx=torem.size()-1; idx>-1; idx-- )
	remove( torem[idx] );
}


void Well::DahObj::addToDahFrom( int fromidx, float extradah )
{
    for ( int idx=fromidx; idx<dah_.size(); idx++ )
	dah_[idx] += extradah;
}


void Well::DahObj::removeFromDahFrom( int fromidx, float extradah )
{
    for ( int idx=fromidx; idx<dah_.size(); idx++ )
	dah_[idx] -= extradah;
}


Well::Data::Data( const char* nm )
    : info_(nm)
    , track_(*new Well::Track)
    , logs_(*new Well::LogSet)
    , disp2d_(*new Well::DisplayProperties(sKey2DDispProp()))
    , disp3d_(*new Well::DisplayProperties(sKey3DDispProp()))
    , d2tmodel_(0)
    , csmodel_(0)
    , markers_(*new MarkerSet)
    , d2tchanged(this)
    , csmdlchanged(this)
    , markerschanged(this)
    , trackchanged(this)
    , disp2dparschanged(this)
    , disp3dparschanged(this)
{
    Strat::LevelSet& lvlset = Strat::eLVLS();
    lvlset.levelToBeRemoved.notify( mCB(this, Well::Data, levelToBeRemoved ) );
}


Well::Data::~Data()
{
    delete &track_;
    delete &logs_;
    delete &disp2d_;
    delete &disp3d_;
    delete d2tmodel_;
    delete csmodel_;
    delete &markers_;

    Strat::eLVLS().levelToBeRemoved.remove(
				mCB(this, Well::Data, levelToBeRemoved ) );
}


void Well::Data::prepareForDelete() const
{
	Well::MGR().removeObject( this );
}


bool Well::Data::haveMarkers() const
{
    return !markers_.isEmpty();
}


bool Well::Data::haveLogs() const
{
    return !logs_.isEmpty();
}


void Well::Data::setD2TModel( D2TModel* d )
{
    if ( d2tmodel_ == d )
	return;
    delete d2tmodel_;
    d2tmodel_ = d;
}


void Well::Data::setCheckShotModel( D2TModel* d )
{
    if ( csmodel_ == d )
	return;
    delete csmodel_;
    csmodel_ = d;
}


void Well::Data::setEmpty()
{
    setD2TModel( 0 ); setCheckShotModel( 0 );
    track_.setEmpty();
    logs_.setEmpty();
    markers_.setEmpty();
}


void Well::Data::levelToBeRemoved( CallBacker* cb )
{
    mDynamicCastGet(Strat::LevelSet*,lvlset,cb)
    if ( !lvlset )
	{ pErrMsg( "cb null or not LevelSet" ); return; }
    const int lvlidx = lvlset->notifLvlIdx();
    if ( lvlset->levels().validIdx( lvlidx ) )
    {
	const Strat::Level& lvl = *lvlset->levels()[lvlidx];
	Well::Marker* mrk = markers().getByLvlID( lvl.id() );
	if ( mrk )
	    mrk->setLevelID( -1 );
    }
}


#define mName "Well name"

void Well::Info::fillPar(IOPar& par) const
{
    par.set( mName, name() );
    par.set( sKeyuwid(), uwid );
    par.set( sKeyoper(), oper );
    par.set( sKeystate(), state );
    par.set( sKeycounty(), county );
    par.set( sKeywelltype(), welltype_ );

    par.set( sKeycoord(), surfacecoord.toString() );
    par.set( sKeyreplvel(), replvel );
    par.set( sKeygroundelev(), groundelev );
}


void Well::Info::usePar( const IOPar& par )
{
    setName( par.find(mName) );
    par.get( sKeyuwid(), uwid );
    par.get( sKeyoper(), oper );
    par.get( sKeystate(), state );
    par.get( sKeycounty(), county );
    int welltype = 0;
    par.get( sKeywelltype(), welltype ); welltype_ = (WellType)welltype;
  
    surfacecoord.fromString( par.find(sKeycoord()) );
    par.get( sKeyreplvel(), replvel );
    par.get( sKeygroundelev(), groundelev );

}

float Well::getDefaultVelocity()
{
    const float replvelm = 2000.f;
    const float replvelft = 8000.f;
    if ( SI().zInFeet() )
	return replvelft;
    else
    {
	return SI().depthsInFeet() ? replvelft * mFromFeetFactorF : replvelm;
    }
}

