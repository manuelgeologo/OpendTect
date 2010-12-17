/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:	Nanne Hemstra
 Date:		October 2008
________________________________________________________________________

-*/
static const char* rcsID = "$Id: flthortools.cc,v 1.35 2010-12-17 06:39:33 raman Exp $";

#include "flthortools.h"

#include "binidvalset.h"
#include "emfaultstickset.h"
#include "emfault3d.h"
#include "emmanager.h"
#include "executor.h"
#include "explfaultsticksurface.h"
#include "explplaneintersection.h"
#include "faultsticksurface.h"
#include "ioman.h"
#include "ioobj.h"
#include "posinfo2d.h"
#include "survinfo.h"
#include "trigonometry.h"


int FaultTrace::nextID( int previd ) const
{ return previd >= -1 && previd < coords_.size()-1 ? previd + 1 : -1; }

void FaultTrace::setIndices( const TypeSet<int>& indices )
{ coordindices_ = indices; }

const TypeSet<int>& FaultTrace::getIndices() const
{ return coordindices_; }

int FaultTrace::add( const Coord3& pos )
{
    coords_ += pos;
    return coords_.size() - 1;
}


int FaultTrace::add( const Coord3& pos, int trcnr )
{
    coords_ += pos;
    trcnrs_ += trcnr;
    return coords_.size() - 1;
}


Coord3 FaultTrace::get( int idx ) const
{ return idx >= 0 && idx < coords_.size() ? coords_[idx] : Coord3::udf(); }

int FaultTrace::getTrcNr( int idx ) const
{ return idx >= 0 && idx < trcnrs_.size() ? trcnrs_[idx] : -1; }

void FaultTrace::set( int idx, const Coord3& pos )
{
    if ( idx >= 0 && idx < coords_.size() )
	coords_[idx] = pos;

}


void FaultTrace::set( int idx, const Coord3& pos, int trcnr )
{
    if ( idx >= 0 && idx < coords_.size() )
    {
	coords_[idx] = pos;
	if ( idx < trcnrs_.size() )
	    trcnrs_[idx] = trcnr;
    }

}


void FaultTrace::remove( int idx )
{
    coords_.remove( idx );
}


bool FaultTrace::isDefined( int idx ) const
{ return idx >= 0 && idx < coords_.size() && coords_[idx] != Coord3::udf(); }

FaultTrace* FaultTrace::clone()
{
    FaultTrace* newobj = new FaultTrace;
    newobj->coords_ = coords_;
    newobj->trcnrs_ = trcnrs_;
    return newobj;
}


bool FaultTrace::getImage( const BinID& bid, float z,
			   const Interval<float>& ztop,
			   const Interval<float>& zbot,
			   const StepInterval<int>& trcrg,
			   BinID& bidimg, float& zimg, bool posdir ) const
{
    float z1 = posdir ? ztop.start : ztop.stop;
    float z2 = posdir ? zbot.start : zbot.stop;
    if ( mIsEqual(z1,z2,1e-6) )
	return false;

    const float frac = ( z - z1 ) / ( z2 - z1 );
    z1 = posdir ? ztop.stop : ztop.start;
    z2 = posdir ? zbot.stop : zbot.start;
    zimg = z1 + frac * ( z2 - z1 );
    BinID start( isinl_ ? nr_ : range_.start, isinl_ ? range_.start : nr_ );
    BinID stop( isinl_ ? nr_ : range_.stop, isinl_ ? range_.stop : nr_ );
    Coord intsectn = getIntersection( start,zimg, stop, zimg );
    if ( intsectn == Coord::udf() )
	return false;

    const float fidx = trcrg.getfIndex( intsectn.x );
    const int index = posdir ? mNINT( ceil(fidx) ) : mNINT( floor(fidx) );
    bidimg.inl = isinl_ ? nr_ : trcrg.atIndex( index );
    bidimg.crl = isinl_ ? trcrg.atIndex( index ) : nr_;
    return true;
}


bool FaultTrace::getHorCrossings( const BinIDValueSet& bvs,
				  Interval<float>& ztop,
				  Interval<float>& zbot ) const
{
    BinID start( isinl_ ? nr_ : range_.start, isinl_ ? range_.start : nr_ );
    float starttopz, startbotz;
    int& startvar = isinl_ ? start.crl : start.inl;
    int step = isinl_ ? SI().crlStep() : SI().inlStep();
    for ( int idx=0; idx<1024; idx++,startvar += step )
    {
	BinIDValueSet::Pos pos = bvs.findFirst( start );
	if ( !pos.valid() )
	    continue;

	BinID dummy;
	bvs.get( pos, dummy, starttopz, startbotz );
	if ( !mIsUdf(starttopz) && !mIsUdf(startbotz) )
	    break;
    }

    BinID stop( isinl_ ? nr_ : range_.stop,
	    	isinl_ ? range_.stop : nr_ );
    int& stopvar = isinl_ ? stop.crl : stop.inl;
    step = -step;
    float stoptopz, stopbotz;
    float prevstoptopz = mUdf(float), prevstopbotz = mUdf(float);
    BinID stoptopbid, stopbotbid, prevstoptopbid, prevstopbotbid;
    bool foundtop = false, foundbot = false;
    for ( int idx=0; idx<1024; idx++,stopvar += step )
    {
	if ( foundtop && foundbot )
	    break;

	BinIDValueSet::Pos pos = bvs.findFirst( stop );
	if ( !pos.valid() )
	    continue;

	BinID dummy;
	float z1=mUdf(float), z2=mUdf(float);
	bvs.get( pos, dummy, z1, z2 );
	if ( !foundtop && !mIsUdf(z1) )
	{
	    stoptopz = z1;
	    Coord intsectn = getIntersection( start, starttopz,
		    			      stop, stoptopz );
	    if ( intsectn == Coord::udf() )
		foundtop = true;
	    else
		prevstoptopz = stoptopz;
	}
	if ( !foundbot && !mIsUdf(z2) )
	{
	    stopbotz = z2;
	    Coord intsectn = getIntersection( start, startbotz,
		    			      stop, stopbotz );
	    if ( intsectn == Coord::udf() )
		foundbot = true;
	    else
		prevstopbotz = stopbotz;
	}
    }

    if ( !foundtop || !foundbot || mIsUdf(prevstoptopz) || mIsUdf(prevstopbotz))
	return false;

    ztop.set( stoptopz, prevstoptopz );
    zbot.set( stopbotz, prevstopbotz );
    return true;
}


float FaultTrace::getZValFor( const BinID& bid ) const
{
    const StepInterval<float>& zrg = SI().zRange( false );
    Coord pt1( isinl_ ? bid.crl : bid.inl, zrg.start * SI().zFactor() );
    Coord pt2( isinl_ ? bid.crl : bid.inl, zrg.stop * SI().zFactor() );
    Line2 line( pt1, pt2 );
    TypeSet<float> intersections;
    for ( int idx=1; idx<coordindices_.size(); idx++ )
    {
	if ( coordindices_[idx] < 0 )
	    idx++;

	const Coord3& pos1 = get( coordindices_[idx-1] );
	const Coord3& pos2 = get( coordindices_[idx] );

	const Coord posbid1 = SI().binID2Coord().transformBackNoSnap( pos1 );
	const Coord posbid2 = SI().binID2Coord().transformBackNoSnap( pos2 );

	Coord nodepos1( isinl_ ? posbid1.y : posbid1.x,
			pos1.z * SI().zFactor() );
	Coord nodepos2( isinl_ ? posbid2.y : posbid2.x,
			pos2.z * SI().zFactor() );
	Line2 fltseg( nodepos1, nodepos2 );
	Coord interpos = line.intersection( fltseg );
	if ( interpos != Coord::udf() )
	    intersections += interpos.y;
    }

    return intersections.size() == 1 ? intersections[0] : mUdf(float);
}


Coord FaultTrace::getIntersection( const BinID& bid1, float z1,
				   const BinID& bid2, float z2  ) const
{
    if ( !getSize() )
	return Coord::udf();

    const bool is2d = trcnrs_.size();
    if ( is2d && trcnrs_.size() != getSize() )
	return Coord::udf();

    Interval<float> zrg( z1, z2 );
    zrg.sort();
    z1 *= SI().zFactor();
    z2 *= SI().zFactor();
    if ( ( isinl_ && (bid1.inl != nr_ || bid2.inl != nr_) )
	    || ( !isinl_ && (bid1.crl != nr_ || bid2.crl != nr_) ) )
	return Coord::udf();

    Coord pt1( isinl_ ? bid1.crl : bid1.inl, z1 );
    Coord pt2( isinl_ ? bid2.crl : bid2.inl, z2 );
    Line2 line( pt1, pt2 );

    for ( int idx=1; idx<coordindices_.size(); idx++ )
    {
	const int curidx = coordindices_[idx];
	const int previdx = coordindices_[idx-1];
	if ( curidx < 0 || previdx < 0 )
	    continue;

	const Coord3& pos1 = get( previdx );
	const Coord3& pos2 = get( curidx );
	if ( ( pos1.z < zrg.start && pos2.z < zrg.start )
		|| ( pos1.z > zrg.stop && pos2.z > zrg.stop ) )
	    continue;

	Coord nodepos1, nodepos2;
	if ( is2d )
	{
	    nodepos1.setXY( getTrcNr(previdx), pos1.z * SI().zFactor() );
	    nodepos2.setXY( getTrcNr(curidx), pos2.z * SI().zFactor() );
	}
	else
	{
	    Coord posbid1 = SI().binID2Coord().transformBackNoSnap( pos1 );
	    Coord posbid2 = SI().binID2Coord().transformBackNoSnap( pos2 );
	    nodepos1.setXY( isinl_ ? posbid1.y : posbid1.x,
		    	   pos1.z * SI().zFactor() );
	    nodepos2.setXY( isinl_ ? posbid2.y : posbid2.x,
		    	   pos2.z * SI().zFactor() );
	}

	Line2 fltseg( nodepos1, nodepos2 );
	Coord interpos = line.intersection( fltseg );
	if ( interpos != Coord::udf() )
	    return interpos;
    }

    return Coord::udf();

}

bool FaultTrace::isCrossing( const BinID& bid1, float z1,
			     const BinID& bid2, float z2  ) const
{
    const Coord intersection = getIntersection( bid1, z1, bid2, z2 );
    return intersection.isDefined();
}


void FaultTrace::computeRange()
{
    range_.set( mUdf(int), -mUdf(int) );
    if ( trcnrs_.size() )
    {
	for ( int idx=0; idx<trcnrs_.size(); idx++ )
	    range_.include( trcnrs_[idx], false );

	return;
    }

    for ( int idx=0; idx<coords_.size(); idx++ )
    {
	const BinID bid = SI().transform( coords_[idx] );
	range_.include( isinl_ ? bid.crl : bid.inl, false );
    }
}


FaultTraceExtractor::FaultTraceExtractor( EM::Fault* flt,
					  int nr, bool isinl )
  : fault_(flt)
  , nr_(nr), isinl_(isinl)
  , flttrc_(0)
  , is2d_(false)
{
    fault_->ref();
}


FaultTraceExtractor::FaultTraceExtractor( EM::Fault* flt,
					  const PosInfo::GeomID& geomid )
  : fault_(flt)
  , nr_(0),isinl_(true)
  , geomid_(geomid)
  , flttrc_(0)
  , is2d_(true)
{
    fault_->ref();
}



FaultTraceExtractor::~FaultTraceExtractor()
{
    fault_->unRef();
    if ( flttrc_ ) flttrc_->unRef();
}


bool FaultTraceExtractor::execute()
{
    if ( flttrc_ )
    {
	flttrc_->unRef();
	flttrc_ = 0;
    }

    if ( is2d_ )
	return get2DFaultTrace();

    EM::SectionID fltsid( 0 );
    mDynamicCastGet(EM::Fault3D*,fault3d,fault_)
    Geometry::IndexedShape* fltsurf = new Geometry::ExplFaultStickSurface(
	    	fault3d->geometry().sectionGeometry(fltsid), SI().zFactor() );
    fltsurf->setCoordList( new FaultTrace, new FaultTrace, 0 );
    if ( !fltsurf->update(true,0) )
	return false;

    CubeSampling cs;
    BinID start( isinl_ ? nr_ : cs.hrg.start.inl,
	    	 isinl_ ? cs.hrg.start.crl : nr_ );
    BinID stop( isinl_ ? nr_ : cs.hrg.stop.inl,
	    	isinl_ ? cs.hrg.stop.crl : nr_ );
    Coord3 p0( SI().transform(start), cs.zrg.start );
    Coord3 p1( SI().transform(start), cs.zrg.stop );
    Coord3 p2( SI().transform(stop), cs.zrg.stop );
    Coord3 p3( SI().transform(stop), cs.zrg.start );
    TypeSet<Coord3> pts;
    pts += p0; pts += p1; pts += p2; pts += p3;
    const Coord3 normal = (p1-p0).cross(p3-p0).normalize();

    Geometry::ExplPlaneIntersection* insectn =
					new Geometry::ExplPlaneIntersection;
    insectn->setShape( *fltsurf );
    insectn->addPlane( normal, pts );
    Geometry::IndexedShape* idxdshape = insectn;
    idxdshape->setCoordList( new FaultTrace, new FaultTrace, 0 );
    if ( !idxdshape->update(true,0) )
	return false;

    Coord3List* clist = idxdshape->coordList();
    mDynamicCastGet(FaultTrace*,flttrc,clist);
    if ( !flttrc ) return false;

    const Geometry::IndexedGeometry* idxgeom = idxdshape->getGeometry()[0];
    if ( !idxgeom ) return false;

    flttrc_ = flttrc->clone();
    flttrc_->ref();
    flttrc_->setIndices( idxgeom->coordindices_ );
    flttrc_->setIsInl( isinl_ );
    flttrc_->setLineNr( nr_ );
    flttrc_->computeRange();
    delete fltsurf;
    delete insectn;
    return true;
}
    

bool FaultTraceExtractor::get2DFaultTrace()
{
    EM::SectionID fltsid( 0 );
    mDynamicCastGet(const EM::FaultStickSet*,fss,fault_)
    if ( !fss ) return false;

    S2DPOS().setCurLineSet( geomid_.lsid_ );
    PosInfo::Line2DData linegeom;
    if ( !S2DPOS().getGeometry(geomid_.lineid_,linegeom) )
	return false;

    const int nrsticks = fss->geometry().nrSticks( fltsid );
    TypeSet<int> indices;
    for ( int sticknr=0; sticknr<nrsticks; sticknr++ )
    {
	PtrMan<IOObj> lsobj =
	    IOM().get( *fss->geometry().lineSet(fltsid,sticknr) );
	if ( !lsobj ) continue;

	const char* linenm = fss->geometry().lineName( fltsid, sticknr );
	PosInfo::GeomID geomid = S2DPOS().getGeomID( lsobj->name(),linenm );
	if ( !(geomid==geomid_) )
	    continue;

	const int nrknots = fss->geometry().nrKnots( fltsid, sticknr );
	const Geometry::FaultStickSet* fltgeom =
	    fss->geometry().sectionGeometry( fltsid );
	if ( !fltgeom || nrknots < 2 )
	    continue;

	if ( !flttrc_ )
	{
	    flttrc_ = new FaultTrace;
	    flttrc_->ref();
	    flttrc_->setIsInl( true );
	    flttrc_->setLineNr( 0 );
	}

	StepInterval<int> colrg = fltgeom->colRange( sticknr );
	for ( int idx=colrg.start; idx<=colrg.stop; idx+=colrg.step )
	{
	    const Coord3 knot = fltgeom->getKnot( RowCol(sticknr,idx) );
	    PosInfo::Line2DPos pos;
	    if ( !linegeom.getPos(knot,pos,1000.0) )
		break;

	    indices += flttrc_->add( knot, pos.nr_ );
	}

	indices += -1;
    }

    if ( !flttrc_ )
	return false;

    flttrc_->setIndices( indices );
    flttrc_->computeRange();
    return flttrc_->getSize() > 1;
}


FaultTraceCalc::FaultTraceCalc( EM::Fault* flt, const HorSampling& hs,
		ObjectSet<FaultTrace>& trcs )
    : Executor("Extracting Fault Traces")
    , hs_(*new HorSampling(hs))
    , flt_(flt)
    , flttrcs_(trcs)
    , nrdone_(0)
    , isinl_(true)
{
    if ( flt )
	flt->ref();

    curnr_ = hs_.start.inl;
}

FaultTraceCalc::~FaultTraceCalc()
{ delete &hs_; if ( flt_ ) flt_->unRef(); }

od_int64 FaultTraceCalc::nrDone() const
{ return nrdone_; }

od_int64 FaultTraceCalc::totalNr() const
{ return hs_.nrInl() + hs_.nrCrl(); }

const char* FaultTraceCalc::message() const
{ return "Extracting Fault Traces"; }

int FaultTraceCalc::nextStep()
{
    if ( !isinl_ && (hs_.nrInl() == 1 || curnr_ > hs_.stop.crl) )
	return Finished();

    if ( isinl_ && (hs_.nrCrl() == 1 || curnr_ > hs_.stop.inl) )
    {
	isinl_ = false;
	curnr_ = hs_.start.crl;
	return MoreToDo();
    }

    FaultTraceExtractor ext( flt_, curnr_, isinl_ );
    if ( !ext.execute() )
	return ErrorOccurred();

    FaultTrace* flttrc = ext.getFaultTrace();
    if ( flttrc )
	flttrc->ref();

    flttrcs_ += flttrc;
    curnr_ += isinl_ ? hs_.step.inl : hs_.step.crl;
    nrdone_++;
    return MoreToDo();
}

