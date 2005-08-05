/*+
 * COPYRIGHT: (C) dGB Beheer B.V.
 * AUTHOR   : K. Tingdahl
 * DATE     : May 2002
-*/

static const char* rcsID = "$Id: viswelldisplay.cc,v 1.45 2005-08-05 15:14:47 cvskris Exp $";

#include "viswelldisplay.h"
#include "viswell.h"
#include "wellman.h"
#include "welllog.h"
#include "welllogset.h"
#include "welldata.h"
#include "welltransl.h"
#include "welltrack.h"
#include "wellmarker.h"
#include "welld2tmodel.h"
#include "iopar.h"
#include "executor.h"
#include "ptrman.h"
#include "survinfo.h"
#include "draw.h"
#include "visdataman.h"


mCreateFactoryEntry( visSurvey::WellDisplay );

namespace visSurvey
{

const char* WellDisplay::earthmodelidstr = "EarthModel ID";
const char* WellDisplay::wellidstr	 = "Well ID";
const char* WellDisplay::log1nmstr	 = "Logname 1";
const char* WellDisplay::log2nmstr	 = "Logname 2";
const char* WellDisplay::log1rgstr	 = "Logrange 1";
const char* WellDisplay::log2rgstr	 = "Logrange 2";
const char* WellDisplay::log1logscstr	 = "Loglogsc 1";
const char* WellDisplay::log2logscstr	 = "Loglogsc 2";
const char* WellDisplay::log1colorstr	 = "Logcolor 1";
const char* WellDisplay::log2colorstr	 = "Logcolor 2";

#define mMeter2Feet(val) \
   val /= 0.3048;

WellDisplay::WellDisplay()
    : well(0)
    , wellid(-1)
    , zistime(SI().zIsTime())
    , zinfeet(SI().zInFeet())
{
    setMaterial(0);
    setWell( visBase::Well::create() );
}


WellDisplay::~WellDisplay()
{
    removeChild( well->getInventorNode() );
    well->unRef();
}


void WellDisplay::setWell( visBase::Well* well_ )
{
    if ( well )
    {
	removeChild( well->getInventorNode() );
	well->unRef();
    }

    well = well_;    
    well->ref();
    addChild( well->getInventorNode() );
}


void WellDisplay::fullRedraw( CallBacker* )
{
    Well::Data* wd = Well::MGR().get( wellid, false );
    
    const Well::D2TModel* d2t = wd->d2TModel();
    setName( wd->name() );

    if ( wd->track().size() < 1 ) return;
    PtrMan<Well::Track> ttrack = 0;
    if ( zistime )
    {
	ttrack = new Well::Track( wd->track() );
	ttrack->toTime( *d2t );
    }
    Well::Track& track = zistime ? *ttrack : wd->track();

    TypeSet<Coord3> trackpos;
    Coord3 pt;
    StepInterval<double> sizrg;
    assign( sizrg, SI().zRange() );
    for ( int idx=0; idx<track.size(); idx++ )
    {
	pt = track.pos( idx );
	if ( zinfeet )
	    mMeter2Feet(pt.z);

	if ( !mIsUndefined(pt.z) && sizrg.includes(pt.z) )
	    trackpos += pt;
    }
    if ( !trackpos.size() )
	return;

    well->setTrack( trackpos );
    well->setWellName( wd->name(), trackpos[0] );
    updateMarkers(0);

    if ( log1nm.size() )
	displayLog( log1nm, log1logsc, log1rg, 1 );

    if ( log2nm.size())
	displayLog( log2nm, log2logsc, log2rg, 2 );
}


#define mErrRet(s) { errmsg = s; return false; }

bool WellDisplay::setWellId( const MultiID& multiid )
{
    Well::Data* wd = Well::MGR().get( multiid, true );
    if ( !wd ) return false;
    
    const Well::D2TModel* d2t = wd->d2TModel();
    if ( zistime )
    {
	if ( !d2t )
	    mErrRet( "No depth to time model defined" )
	wd->d2tchanged.notify( mCB(this,WellDisplay,fullRedraw) );
    }

    wellid = multiid;
    fullRedraw(0);
    wd->markerschanged.notify( mCB(this,WellDisplay,updateMarkers) );

    return true;
}


const LineStyle* WellDisplay::lineStyle() const
{
    return &well->lineStyle();
}


void WellDisplay::setLineStyle( const LineStyle& lst )
{
    well->setLineStyle( lst );
}


void WellDisplay::updateMarkers( CallBacker* )
{
    Well::Data* wd = Well::MGR().get( wellid );
    if ( !wd ) return;

    well->removeAllMarkers();
    for ( int idx=0; idx<wd->markers().size(); idx++ )
    {
	Well::Marker* wellmarker = wd->markers()[idx];
	Coord3 pos = wd->track().getPos( wellmarker->dah );
	if ( !pos.x && !pos.y && !pos.z ) continue;

	if ( zistime )
	    pos.z = wd->d2TModel()->getTime( wellmarker->dah );
	else if ( zinfeet )
	    mMeter2Feet(pos.z)

	well->addMarker( pos, wellmarker->color, wellmarker->name() );
    }
}


void WellDisplay::setMarkerScreenSize( int sz )
{ well->setMarkerScreenSize( sz ); }

int WellDisplay::markerScreenSize() const
{ return well->markerScreenSize(); }


#define mShowFunction( showObj, objShown ) \
void WellDisplay::showObj( bool yn ) \
{ \
    well->showObj( yn ); \
} \
\
bool WellDisplay::objShown() const \
{ \
    return well->objShown(); \
}

bool WellDisplay::canShowMarkers() const
{ return well->canShowMarkers(); }

mShowFunction( showWellName, wellNameShown )
mShowFunction( showMarkers, markersShown )
mShowFunction( showMarkerName, markerNameShown )
mShowFunction( showLogs, logsShown )
mShowFunction( showLogName, logNameShown )


void WellDisplay::displayLog( int logidx, int lognr, bool logrthm,
			      const Interval<float>* range )
{
    Well::Data* wd = Well::MGR().get( wellid );
    if ( !wd || !wd->logs().size() ) return;

    Well::Log& log = wd->logs().getLog(logidx);
    const int logsz = log.size();
    if ( !logsz ) return;

    Well::Track& track = wd->track();
    TypeSet<Coord3Value> crdvals;
    StepInterval<double> sizrg;
    assign( sizrg, SI().zRange() );
    for ( int idx=0; idx<logsz; idx++ )
    {
	const float dah = log.dah(idx);
	Coord3 pos = track.getPos( dah );
	if ( !pos.x && !pos.y && !pos.z ) continue;

	if ( zistime )
	    pos.z = wd->d2TModel()->getTime( dah );
	else if ( zinfeet )
	    mMeter2Feet(pos.z)

	if ( !sizrg.includes(pos.z) )
	{
	    if ( pos.z > sizrg.stop ) break;
	    continue;
	}

	Coord3Value cv( pos, log.value(idx) );
	crdvals += cv;
    }

    Interval<float> selrange;
    assign( selrange, range ? *range : log.selValueRange() );
    if ( !range )
	logrthm = log.dispLogarithmic();
    well->setLogData( crdvals, log.name(), selrange, logrthm, lognr );

    if ( lognr == 1 )
	{ log1nm = log.name(); assign(log1rg,selrange); log1logsc = logrthm; }
    else
	{ log2nm = log.name(); assign(log2rg,selrange); log2logsc = logrthm; }
}


void WellDisplay::displayLog( const char* lognm, bool logarthm,
			      const Interval<float>& range, int lognr )
{
    Well::Data* wd = Well::MGR().get( wellid );
    if ( !wd || !wd->logs().size() ) return;

    int logidx = -1;
    for ( int idx=0; idx<wd->logs().size(); idx++ )
    {
	const char* nm = wd->logs().getLog(idx).name();
	if ( !strcmp(lognm,nm) ) { logidx = idx; break; }
    }

    if ( logidx < 0 ) return; // TODO: errmsg
    
    displayLog( logidx, lognr, logarthm, &range );
}


void WellDisplay::setLogColor( const Color& col, int lognr )
{ well->setLogColor( col, lognr ); }


const Color& WellDisplay::logColor( int lognr ) const
{ return well->logColor( lognr ); }


void WellDisplay::setLogLineWidth( float width, int lognr )
{ well->setLogLineWidth( width, lognr ); }


float WellDisplay::logLineWidth( int lognr ) const
{ return well->logLineWidth( lognr ); }


void WellDisplay::setLogWidth( int width )
{ well->setLogWidth( width ); }


int WellDisplay::logWidth() const
{ return well->logWidth(); }


void WellDisplay::fillPar( IOPar& par, TypeSet<int>& saveids ) const
{
    visBase::VisualObjectImpl::fillPar( par, saveids );

    par.set( earthmodelidstr, wellid );

    int viswellid = well->id();
    par.set( wellidstr, viswellid );
    if ( saveids.indexOf(viswellid) == -1 ) saveids += viswellid;

    par.set( log1nmstr, log1nm );
    par.set( log1rgstr, log1rg.start, log1rg.stop );
    par.setYN( log1logscstr, log1logsc );
    BufferString colstr;
    logColor(1).fill( colstr.buf() );
    par.set( log1colorstr, colstr );

    par.set( log2nmstr, log2nm );
    par.set( log2rgstr, log2rg.start, log2rg.stop );
    par.setYN( log2logscstr, log2logsc );
    logColor(2).fill( colstr.buf() );
    par.set( log2colorstr, colstr );
}


int WellDisplay::usePar( const IOPar& par )
{
    int res = visBase::VisualObjectImpl::usePar( par );
    if ( res!=1 ) return res;

    int viswellid;
    if ( par.get(wellidstr,viswellid) )
    {
	DataObject* dataobj = visBase::DM().getObject( viswellid );
	if ( !dataobj ) return 0;
	mDynamicCastGet(visBase::Well*,well_,dataobj)
	if ( !well_ ) return -1;
	setWell( well_ );
    }
    else
    {
	setWell( visBase::Well::create() );
	viswellid = well->id();
    }
    
    setDisplayTransformation( SPM().getUTM2DisplayTransform() );

    MultiID newmid;
    if ( !par.get(earthmodelidstr,newmid) )
	return -1;

    if ( !setWellId(newmid) )
    {
	return 1;
    }

    BufferString log1nm_;
    par.get( log1nmstr, log1nm_ );
    par.get( log1rgstr, log1rg.start, log1rg.stop );
    par.getYN( log1logscstr, log1logsc );
    if ( *log1nm_.buf() )
	displayLog( log1nm_, log1logsc, log1rg, 1 );
    BufferString colstr; Color col;
    par.get( log1colorstr, colstr );
    if ( col.use(colstr.buf()) )
	setLogColor( col, 1 );

    BufferString log2nm_;
    par.get( log2nmstr, log2nm_ );
    par.get( log2rgstr, log2rg.start, log2rg.stop );
    par.getYN( log2logscstr, log2logsc );
    if ( *log2nm_.buf() )
	displayLog( log2nm_, log2logsc, log2rg, 2 );
    par.get( log2colorstr, colstr );
    if ( col.use(colstr.buf()) )
	setLogColor( col, 2 );

// Support for old sessions
    BufferString linestyle;
    if ( par.get(visBase::Well::linestylestr,linestyle) )
    {
	LineStyle lst;
	lst.fromString( linestyle );
	setLineStyle( lst );
    }

    bool wellnmshown;
    if ( par.getYN(visBase::Well::showwellnmstr,wellnmshown) )
	showWellName( wellnmshown );

    return 1;
}


void WellDisplay::setDisplayTransformation( visBase::Transformation* nt )
{
    well->setDisplayTransformation( nt );
}


visBase::Transformation* WellDisplay::getDisplayTransformation()
{ return well->getDisplayTransformation(); }

}; // namespace visSurvey
