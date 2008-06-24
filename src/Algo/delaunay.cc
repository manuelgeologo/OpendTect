/*+
 * COPYRIGHT: (C) dGB Beheer B.V.
 * AUTHOR   : Y.C. Liu
 * DATE     : January 2008
-*/

static const char* rcsID = "$Id: delaunay.cc,v 1.13 2008-06-24 18:45:19 cvskris Exp $";

#include "delaunay.h"
#include "trigonometry.h"


ParallelDTriangulator::ParallelDTriangulator( DAGTriangleTree& dagt )
    : tree_( dagt )  
    , israndom_( true )
{}


int ParallelDTriangulator::totalNr() const                
{ return tree_.coordList().size(); }


bool ParallelDTriangulator::doPrepare( int nrthreads )
{
    const int nrcoords = totalNr();
    if ( israndom_ )
	permutation_.erase();
    else
    {
	int arr[nrcoords];
	for ( int idx=0; idx<nrcoords; idx++ )
	    arr[idx] = idx;

	std::random_shuffle( arr, arr+nrcoords );
	for ( int idx=0; idx<nrcoords; idx++ )
	    permutation_ += arr[idx];
    }
    
    return true;
}


bool ParallelDTriangulator::doWork( int start, int stop, int threadid )
{
    for ( int idx=start; idx<=stop && shouldContinue(); idx++, reportNrDone(1) )
    {
	const int insertptid = permutation_.size() ? permutation_[idx] : idx;
	int dupid;
       	if ( !tree_.insertPoint( insertptid, dupid ) )
	    return false;
    }

    return true;
}


DAGTriangleTree::DAGTriangleTree()
    : coordlist_( 0 )
    , epsilon_( 1e-5 )
    , ownscoordlist_( true )
{}


DAGTriangleTree::DAGTriangleTree( const DAGTriangleTree& b )
    : coordlist_( 0 )
    , epsilon_( 1e-5 )
    , ownscoordlist_( true )
{
    *this = b;
}


DAGTriangleTree& DAGTriangleTree::operator=( const DAGTriangleTree& b )
{
    epsilon_ = b.epsilon_;
    if ( ownscoordlist_ )
	delete coordlist_;

    if ( b.ownscoordlist_ )
	coordlist_ = b.coordlist_ ? new TypeSet<Coord>( *b.coordlist_ ) : 0;
    else
	coordlist_ = b.coordlist_;

    ownscoordlist_ = b.ownscoordlist_;

    triangles_ = b.triangles_;

    initialcoords_[0] = b.initialcoords_[0];
    initialcoords_[1] = b.initialcoords_[1];
    initialcoords_[2] = b.initialcoords_[2];
    return *this;
}


DAGTriangleTree::~DAGTriangleTree()
{
    if ( ownscoordlist_ && coordlist_ )
	coordlist_->erase();
}


bool DAGTriangleTree::computeCoordRanges( const TypeSet<Coord>& coordlist, 
					  Interval<double>& xrg,
					  Interval<double>& yrg )
{
    if ( !coordlist.size() )
	return false;

    xrg.start = xrg.stop = coordlist[0].x;
    yrg.start = yrg.stop = coordlist[0].y;

    for ( int idx=1; idx<coordlist.size(); idx++ )
    {
	xrg.include( coordlist[idx].x );
	yrg.include( coordlist[idx].y );
    }

    return true;
}


bool DAGTriangleTree::setCoordList( const TypeSet<Coord>& coordlist, bool copy )
{
    coordlock_.writeLock();
    if ( coordlist_ && ownscoordlist_ )
	delete coordlist_;

    coordlist_ = 0;
    coordlock_.writeUnLock();

    if ( coordlist.size()<3 )
	return false;

    Interval<double> xrg, yrg;
    computeCoordRanges( coordlist, xrg, yrg );

    coordlock_.writeLock();

    if ( copy )
    {
	coordlist_ = new TypeSet<Coord>( coordlist );
	ownscoordlist_ = true;
    }
    else
    {
	coordlist_ = const_cast<TypeSet<Coord>* >( &coordlist );
	ownscoordlist_ = false;
    }

    coordlock_.writeUnLock();
    return setBBox( xrg, yrg );
}


bool DAGTriangleTree::setBBox(const Interval<double>& xrg,
			  const Interval<double>& yrg)
{
    triangles_.erase();
    const double xlength = xrg.width();
    const double ylength = yrg.width();
    if ( mIsZero(xlength,1e-4) || mIsZero(ylength,1e-4) )
	return false;

    const Coord center( xrg.center(), yrg.center() );
    const double radius = 2*sqrt( xlength*xlength+ylength*ylength );
    initialcoords_[0] = Coord( center.x-radius*sqrt(3), center.y-radius );
    initialcoords_[1] = Coord( center.x+radius*sqrt(3), center.y-radius );
    initialcoords_[2] = Coord( center.x, center.y+2*radius );

    DAGTriangle initnode;
    initnode.coordindices_[0] = cInitVertex0();
    initnode.coordindices_[1] = cInitVertex1();
    initnode.coordindices_[2] = cInitVertex2();
    triangles_ +=initnode;

    return true;
}


bool DAGTriangleTree::init()
{
    if ( !triangles_.size() ) return false;

    coordlock_.writeLock();
    if ( !coordlist_ )
    {
	coordlist_ = new TypeSet<Coord>;
	ownscoordlist_ = true;
    }
    coordlock_.writeUnLock();

    return true;
}


int DAGTriangleTree::insertPoint( const Coord& coord, int& dupid )
{
    if ( !ownscoordlist_ )
	return cNoVertex();

    coordlock_.writeLock();
    const int ci = coordlist_->size();
    (*coordlist_) += coord;
    coordlock_.writeUnLock();

    if ( !insertPoint( ci, dupid ) )
    {
	coordlock_.writeLock();
	if ( coordlist_->size()==ci+1 )
	    coordlist_->remove( ci );
	coordlock_.writeUnLock();

	return cNoVertex();
    }

    return ci;
}


bool DAGTriangleTree::getTriangle( const Coord& coord, int& dupid,
			TypeSet<int>& vertices ) const
{
    dupid = cNoVertex();
    vertices.erase();
    if ( !ownscoordlist_ )
	return false;

    coordlock_.writeLock();
    const int ci = coordlist_->size();
    (*coordlist_) += coord;
    coordlock_.writeUnLock();

    int ti0, ti1;
    const char res = searchTriangle( ci, 0, ti0, ti1, dupid );
    if ( dupid!=cNoVertex() )
	return true;

    if ( ti0==cNoVertex() && ti1==cNoVertex() )
	return false;

    const int* crds0 = triangles_[ti0].coordindices_;
    const int* crds1 = ti1!=cNoVertex() ? triangles_[ti1].coordindices_ : 0;
    if ( crds0[0]>=0 && crds0[1]>=0 && crds0[2]>=0 )
    {
    	vertices += crds0[0];
    	vertices += crds0[1];
    	vertices += crds0[2];
	return true;
    }
    
    if ( crds1 && crds1[0]>=0 && crds1[1]>=0 && crds1[2]>=0 )
    {
    	vertices += crds1[0];
    	vertices += crds1[1];
    	vertices += crds1[2];
	return true;
    }

   return false;
}


bool DAGTriangleTree::insertPoint( int ci, int& dupid )
{
    dupid = cNoVertex();
    coordlock_.readLock();
    if ( mIsUdf((*coordlist_)[ci].x) || mIsUdf((*coordlist_)[ci].y) ) 
    {
	coordlock_.readUnLock();
	BufferString msg = "The point ";
	msg += ci;
	msg +=" is not defined!";
	pErrMsg( msg );
	return true; //For undefined point, skip.
    }

    coordlock_.readUnLock();

    int ti0, ti1;
    const char res = searchTriangle( ci, 0, ti0, ti1, dupid );
    
    if ( res==cIsInside() || res==cIsOnEdge() )
    {
	rwlock_.permissiveWriteLock();
	int nti0 = ti0, nti1 = ti1;
	const char nres = searchFurther( ci, nti0, nti1, dupid );

	if ( nres==cIsInside() || nres==cIsOnEdge() )
	{
	    if ( nti1==cNoTriangle() )
		splitTriangleInside( ci, nti0 );
	    else
		splitTriangleOnEdge( ci, nti0, nti1 );
	    
	    rwlock_.permissiveWriteUnLock();
	    return true;
	}
	else
	{
	    rwlock_.permissiveWriteUnLock();

	    if ( nres==cIsDuplicate() )
		return true;
	    else
	    {
		BufferString msg = "\n Insert point ";
		msg += ci;
		msg += "failed!";
		pErrMsg( msg );
		return false;
	    }
	}
    }
    else if ( res==cIsDuplicate() )
	return true;
    else  
    {
	BufferString msg = "\n Insert point ";
	msg += ci;
	msg += "failed!";
	pErrMsg( msg );
	return false;
    }

    return true;
}


#define mCrd( idx ) \
	(idx>=0 ? (*coordlist_)[idx] : initialcoords_[-idx-2])


int DAGTriangleTree::searchNeighbor( int ti, char edge ) const
{
    const int* crd = triangles_[ti].coordindices_;
    const int* nbor = triangles_[ti].neighbors_;

    int neighbor = cNoTriangle();

    if ( edge==0 )
	neighbor = searchChild( crd[0], crd[1], nbor[0] );
    else if ( edge==1 )
	neighbor = searchChild( crd[1], crd[2], nbor[1] );
    else if ( edge==2 )
	neighbor = searchChild( crd[2], crd[0], nbor[2] );
    else
    {
	pErrMsg("Should not happen");
    }

    return neighbor;
}


char DAGTriangleTree::searchTriangle( int ci, int startti, int& ti0, int& ti1,
				      int& dupid ) const
{
    if ( startti<0 )
	startti = 0;

    ti0 = startti;
    ti1 = cNoTriangle();

    return searchFurther( ci,  ti0, ti1, dupid );
}


char DAGTriangleTree::searchFurther( int ci, int& ti0, int& ti1,
				     int& dupid ) const
{
    while ( true )
    {
	const int curtriangle = ti0;

	if ( ti1==cNoTriangle() ) //I.e I must be inside my ti0
	{
	    rwlock_.readLock();
	    if (  !triangles_[curtriangle].hasChildren() )
	    {
		rwlock_.readUnLock();
		return cIsInside();
	    }

	    const int* cptr = triangles_[curtriangle].childindices_;
	    const int children[] = { cptr[0], cptr[1], cptr[2] };
	    rwlock_.readUnLock();
	   
	    bool found = false; 
	    for ( int childidx = 0; childidx<3; childidx++ )
	    {
		const int curchild = children[childidx];
		if ( curchild==cNoTriangle() ) continue;

		char edge;
		const char mode = isInside( ci, curchild, edge, dupid  );
		if ( mode==cIsOutside() ) 
		    continue;

		if ( mode==cIsDuplicate() )
		    return mode;

		if ( mode==cIsInside() )
		{
		    ti0 = curchild;
		    ti1 = cNoTriangle();

		    rwlock_.readLock();
		    const bool haschildren = triangles_[curchild].hasChildren();
		    rwlock_.readUnLock();
		    found = true;

		    if ( !haschildren )
			return cIsInside();

		    break;
		}
		else if ( mode==cIsOnEdge() )
		{
		    ti0 = curchild;
		    ti1 = triangles_[curchild].neighbors_[edge];
		    found = true;
		    break;
		}
		else
		{
		    pErrMsg("Hmm");
		}
	    }

	    if ( !found )
	    {
		pErrMsg("No child found");
		return cError();
	    }
	}
	else
	{
	    rwlock_.readLock();
	    if ( !triangles_[curtriangle].hasChildren() )
	    {
		if ( triangles_[ti1].hasChildren() )
		{
		    rwlock_.readUnLock();
		    const char edge = getCommonEdge( ti0, ti1);
		    if ( edge<0 )
		    {
			pErrMsg("Hmm");
			return cError();
		    }

		    ti1 = searchNeighbor( ti0, edge);
		    if ( ti1==cNoTriangle() )
		    {
			pErrMsg("Hmm");
			return cError();
		    }
		}
		else
		    rwlock_.readUnLock();

		return cIsOnEdge();
	    }

	    rwlock_.readUnLock();

	    char edge;
	    const char res =
		searchTriangleOnEdge( ci, curtriangle,ti0, edge, dupid);
	    if ( res==cIsInside() )
	    {
		if ( ti0==cNoTriangle() )
		    return cError();

		ti1 = cNoTriangle();
	    }
	    else if ( res==cIsDuplicate() )
		return res;
	    else if ( res==cIsOnEdge() )
	    {
		ti1 = searchNeighbor( ti0, edge );
		if ( ti1==cNoTriangle() )
		    return cError();

		return cIsOnEdge();
	    }
	    else
	    {
		pErrMsg( "Should not happen" );
		return cError();
	    }
	}
    }

    return cError();
}


char DAGTriangleTree::searchTriangleOnEdge( int ci, int ti, int& resti,
				char& edge, int& dupid ) const
{
    rwlock_.readLock();

    if ( !triangles_[ti].hasChildren() )
    {
	rwlock_.readUnLock();
	return cError();
    }

    const int* cptr = triangles_[ti].childindices_;
    const int children[] = { cptr[0], cptr[1], cptr[2] };

    rwlock_.readUnLock();

    for ( int idx=0; idx<3; idx++ )
    {
	const int curchild = children[idx];
	if ( curchild==cNoTriangle() )
	    continue;

    	const char inchild = isInside( ci, curchild, edge, dupid );
	if ( inchild==cIsDuplicate() )
	    return cIsDuplicate();
	else if ( inchild==cIsOnEdge() )
	{
	    rwlock_.readLock();
	    const bool haschildren = triangles_[curchild].hasChildren();
	    rwlock_.readUnLock();

	    if ( haschildren )
		return searchTriangleOnEdge( ci, curchild, resti, edge,
					     dupid );
	    resti = curchild;
	    return cIsOnEdge();
	}
	else if ( inchild==cIsInside() )
	{
	    resti = curchild;

	    return cIsInside();
	}
    }

    pErrMsg( "This should never happen." );
    resti = cNoTriangle();

    return cError();
}


char DAGTriangleTree::isOnEdge( const Coord& p, const Coord& a, 
				const Coord& b, bool& duponfirst ) const
{
    const Line3 line( Coord3(a,0), Coord3(b,0)-Coord3(a,0) );
    double t = line.closestPoint( Coord3(p,0) );
    if ( t<0 || t>1 )
	return cNotOnEdge();

    const Coord interectpos = line.getPoint( t );
    const double sqdist = p.sqDistTo( interectpos );
    if ( sqdist>epsilon_*epsilon_ )
	return cNotOnEdge();

    if ( mIsZero(t,1e-5) )
    {
	duponfirst = true;
	return cIsDuplicate();
    }
    else if ( mIsEqual(t,1,1e-5) )
    {
	duponfirst = false;
	return cIsDuplicate();
    }

    return cIsOnEdge();
}


char DAGTriangleTree::isInside( int ci, int ti, char& edge, int& dupid ) const
{
    if ( ti==cNoTriangle() )
	return cIsOutside();

    const int* crds = triangles_[ti].coordindices_;
    coordlock_.readLock();
    const Coord& coord = mCrd(ci);
    const Coord& tricoord0 = mCrd(crds[0]);
    const Coord& tricoord1 = mCrd(crds[1]);
    const Coord& tricoord2 = mCrd(crds[2]);
    if ( pointInTriangle2D( coord, tricoord0, tricoord1, tricoord2, 0 ) )
    {
	coordlock_.readUnLock();
	return cIsInside();
    }

    bool duponfirst;
    char res = isOnEdge( coord, tricoord0, tricoord1, duponfirst );
    if ( res!=cNotOnEdge() )
    {
	if ( res==cIsDuplicate() ) dupid = duponfirst ? crds[0] : crds[1];
	else edge = 0;
	coordlock_.readUnLock();
	return res;
    }

    res = isOnEdge( coord, tricoord1, tricoord2, duponfirst );
    if ( res!=cNotOnEdge() )
    {
	if ( res==cIsDuplicate() ) dupid = duponfirst ? crds[1] : crds[2];
	else edge = 1;
	coordlock_.readUnLock();
	return res;
    }

    res = isOnEdge( coord, tricoord2, tricoord0, duponfirst );
    coordlock_.readUnLock();

    if ( res!=cNotOnEdge() )
    {
	if ( res==cIsDuplicate() ) dupid = duponfirst ? crds[2] : crds[0];
	else edge = 2;
	return res;
    }

    return cIsOutside();
}


void DAGTriangleTree::splitTriangleInside( int ci, int ti )
{
    if ( ti<0 || ti>=triangles_.size() )
	return;

    const int crd0 = triangles_[ti].coordindices_[0];
    const int crd1 = triangles_[ti].coordindices_[1];
    const int crd2 = triangles_[ti].coordindices_[2];
    const int* nbti = triangles_[ti].neighbors_;

    const int ti0 = triangles_.size();
    const int ti1 = triangles_.size()+1;
    const int ti2 = triangles_.size()+2;

    DAGTriangle child0;
    child0.coordindices_[0] = crd0;
    child0.coordindices_[1] = crd1;
    child0.coordindices_[2] = ci;
    child0.neighbors_[0] = searchChild( crd0, crd1,nbti[0] );
    child0.neighbors_[1] = ti2;
    child0.neighbors_[2] = ti1;

    DAGTriangle child1;
    child1.coordindices_[0] = crd0;
    child1.coordindices_[1] = ci; 
    child1.coordindices_[2] = crd2;
    child1.neighbors_[0] = ti0;
    child1.neighbors_[1] = ti2;
    child1.neighbors_[2] = searchChild( crd2, crd0, nbti[2] );
    
    DAGTriangle child2;
    child2.coordindices_[0] = ci; 
    child2.coordindices_[1] = crd1;
    child2.coordindices_[2] = crd2;
    child2.neighbors_[0] = ti0;
    child2.neighbors_[1] = searchChild( crd1, crd2, nbti[1] );
    child2.neighbors_[2] = ti1;

    rwlock_.convPermissiveToWriteLock();
    triangles_ += child0;
    triangles_ += child1;
    triangles_ += child2;

    triangles_[ti].childindices_[0] = ti0;
    triangles_[ti].childindices_[1] = ti1;
    triangles_[ti].childindices_[2] = ti2;
    rwlock_.convWriteToPermissive();

    TypeSet<char> v0s; 
    TypeSet<char> v1s; 
    TypeSet<int> tis; 

    v0s += 0; v1s += 1; tis += ti0;
    v0s += 0; v1s += 2; tis += ti1;
    v0s += 1; v1s += 2; tis += ti2;

    legalizeTriangles( v0s, v1s, tis );
}


int DAGTriangleTree::getNeighbor( int v0, int v1, int ti ) const
{
    if ( ti==cNoTriangle() )
	return cNoVertex();

    const int* crds = triangles_[ti].coordindices_;
    int id0=-1;
    int id1=-1;
    for ( int idx=0; idx<3; idx++ )
    {
	if ( crds[idx]==v0 )
	    id0 = idx;
	
	if ( crds[idx]==v1 )
	    id1 = idx;
    }

    if ( id0==-1 || id1==-1 )
    {
	pErrMsg( "vertex is not on the triangle" );
	return cNoVertex();
    }

    int res;
    
    if ( id0==0 && id1==1 || id0==1 && id1==0 )
	res = searchChild( v0, v1, triangles_[ti].neighbors_[0] );
    if ( id0==0 && id1==2 || id0==2 && id1==0 )
	res = searchChild( v0, v1, triangles_[ti].neighbors_[2] );
    else if ( id0==1 && id1==2 || id0==2 && id1==1 )
	res = searchChild( v0, v1, triangles_[ti].neighbors_[1] );

    return res;
}


void DAGTriangleTree::splitTriangleOnEdge( int ci, int ti0, int ti1 )
{
    if ( ti0==cNoTriangle() || ti1==cNoTriangle() )
	return; 

    const int* crds0 = triangles_[ti0].coordindices_;
    const int* crds1 = triangles_[ti1].coordindices_;
    int shared0 = cNoVertex();
    int shared1 = cNoVertex();
    int vti0 = cNoVertex();
    int vti1 = cNoVertex();

    for ( int idx=0; idx<3; idx++ )
    {
	if ( crds0[idx]!=crds1[0] && crds0[idx]!=crds1[1] && 
		crds0[idx]!=crds1[2] )
	{
	    vti0 = crds0[idx];
	    if ( idx==0 )
	    {
		shared0 = crds0[1];
		shared1 = crds0[2];
	    }
	    else if ( idx==1 )
	    {           
		shared0 = crds0[2];
		shared1 = crds0[0];
	    }
	    else
	    {
		shared0 = crds0[0];
		shared1 = crds0[1]; 
	    }
	}

	if ( crds1[idx]!=crds0[0] && crds1[idx]!=crds0[1] &&
		crds1[idx]!=crds0[2] )
	    vti1 = crds1[idx];
    }

    if ( shared0==cNoVertex() || shared1==cNoVertex() ||
	 vti0==cNoVertex() || vti1==cNoVertex() )
    {
	pErrMsg( "Two triangles don't share edge" );
	return;
    }

    const int nti0 = triangles_.size();
    const int nti1 = triangles_.size()+1;
    const int nti2 = triangles_.size()+2;
    const int nti3 = triangles_.size()+3;

    DAGTriangle child0;
    child0.coordindices_[0] = shared0;
    child0.coordindices_[1] = ci;
    child0.coordindices_[2] = vti0;
    child0.neighbors_[0] = nti2;
    child0.neighbors_[1] = nti1;
    child0.neighbors_[2] = getNeighbor(vti0,shared0,ti0 );

    DAGTriangle child1;
    child1.coordindices_[0] = shared1;
    child1.coordindices_[1] = vti0;
    child1.coordindices_[2] = ci; 
    child1.neighbors_[0] = getNeighbor(shared1,vti0,ti0 );
    child1.neighbors_[1] = nti0;
    child1.neighbors_[2] = nti3;

    DAGTriangle child2;
    child2.coordindices_[0] = shared0;
    child2.coordindices_[1] = vti1;
    child2.coordindices_[2] = ci; 
    child2.neighbors_[0] = getNeighbor(shared0,vti1,ti1 );
    child2.neighbors_[1] = nti3;
    child2.neighbors_[2] = nti0;

    DAGTriangle child3;
    child3.coordindices_[0] = shared1;
    child3.coordindices_[1] = ci;;
    child3.coordindices_[2] = vti1; 
    child3.neighbors_[0] = nti1;
    child3.neighbors_[1] = nti2;
    child3.neighbors_[2] = getNeighbor(vti1,shared1,ti1 );

    rwlock_.convPermissiveToWriteLock();
    triangles_ += child0;
    triangles_ += child1;
    triangles_ += child2;
    triangles_ += child3;
    triangles_[ti0].childindices_[0] = nti0;
    triangles_[ti0].childindices_[1] = nti1;
    triangles_[ti1].childindices_[0] = nti2;
    triangles_[ti1].childindices_[1] = nti3;
    rwlock_.convWriteToPermissive();

    TypeSet<char> v0s; 
    TypeSet<char> v1s; 
    TypeSet<int> tis; 

    v0s += 0; v1s += 2; tis += nti0;
    v0s += 0; v1s += 1; tis += nti1;
    v0s += 0; v1s += 1; tis += nti2;
    v0s += 0; v1s += 2; tis += nti3;

    legalizeTriangles( v0s, v1s, tis );
}


void DAGTriangleTree::legalizeTriangles( TypeSet<char>& v0s, TypeSet<char>& v1s, 				TypeSet<int>& tis )
{
    int start = 0;
    while ( v0s.size()>start )
    {
	const char v0 = v0s[start]; 
	const char v1 = v1s[start];
	const int ti = tis[start];
	if ( ti<0 )
	    continue;
	
	if ( start>10000 )
	{
	    v0s.remove( 0, start );
	    v1s.remove( 0, start );
	    tis.remove( 0, start );
	    start = 0;
	}
	else
  	    start++;


	int shared0, shared1, crdci;
	int checkti = cNoTriangle();
	if ( v0==0 && v1==1 || v0==1 && v1==0 )
	{
	    checkti = triangles_[ti].neighbors_[0];
	    crdci = triangles_[ti].coordindices_[2];
	    if ( v0==0 && v1==1 )
	    {
		shared0 =  triangles_[ti].coordindices_[0];
		shared1 =  triangles_[ti].coordindices_[1];  
	    }
	    else
	    {
		shared0 = triangles_[ti].coordindices_[1];
		shared1 = triangles_[ti].coordindices_[0];   
	    }
	}
	else if ( v0==0 && v1==2 || v0==2 && v1==0 )
	{
	    checkti = triangles_[ti].neighbors_[2];
	    crdci = triangles_[ti].coordindices_[1];
	    if ( v0==0 && v1==2 )
	    {
		shared0 = triangles_[ti].coordindices_[2];
		shared1 = triangles_[ti].coordindices_[0];   
	    }
	    else
	    {
		shared0 = triangles_[ti].coordindices_[0];
		shared1 = triangles_[ti].coordindices_[2];   
	    }
	}
	else if ( v0==1 && v1==2 || v0==2 && v1==1 )
	{
	    checkti = triangles_[ti].neighbors_[1];
	    crdci = triangles_[ti].coordindices_[0];
	    if ( v0==1 && v1==2 )
	    {
		shared0 = triangles_[ti].coordindices_[1];
		shared1 = triangles_[ti].coordindices_[2];   
	    }
	    else
	    {
		shared0 = triangles_[ti].coordindices_[2];
		shared1 = triangles_[ti].coordindices_[1];   
	    }
	}
	
	if ( checkti==cNoTriangle() )
	    continue;

	const int* checkcrds = triangles_[checkti].coordindices_;
	int checkpt =cNoVertex();
	for ( int idx=0; idx<3; idx++ )
	{
	    if ( checkcrds[idx] != shared0 && checkcrds[idx] != shared1 )
	    {
		checkpt = checkcrds[idx];
		break;
	    }
	}

	if ( checkpt==cNoVertex() )
	{
	    pErrMsg("Impossible case.");
	    continue;
	}

	coordlock_.readLock();
	if ( !isInsideCircle(mCrd(checkpt), mCrd(crdci), mCrd(shared0),
		    mCrd(shared1)) || checkpt==crdci )
	{
	    coordlock_.readUnLock();
	    continue;
	}

	coordlock_.readUnLock();
	
	const int nti0 = triangles_.size();
	const int nti1 = triangles_.size()+1;
   
	DAGTriangle child0;
	child0.coordindices_[0] = crdci;
	child0.coordindices_[1] = shared0; 
	child0.coordindices_[2] = checkpt;
	child0.neighbors_[0] = getNeighbor(shared0,crdci,ti );
	child0.neighbors_[1] = getNeighbor(checkpt,shared0, checkti );
	child0.neighbors_[2] = nti1;

	DAGTriangle child1;
	child1.coordindices_[0] = shared1;
	child1.coordindices_[1] = crdci; 
	child1.coordindices_[2] = checkpt; 
	child1.neighbors_[0] = getNeighbor(crdci,shared1,ti );
	child1.neighbors_[1] = nti0;
	child1.neighbors_[2] = getNeighbor(shared1,checkpt, checkti );

	rwlock_.convPermissiveToWriteLock();

	triangles_ += child0;
	triangles_ += child1;

	triangles_[ti].childindices_[0] = nti0;
	triangles_[ti].childindices_[1] = nti1;
	triangles_[checkti].childindices_[0] = nti0;
	triangles_[checkti].childindices_[1] = nti1;
	rwlock_.convWriteToPermissive();

	v0s += 1; v1s += 2; tis += nti0;
	v0s += 0; v1s += 2; tis += nti1;
    }
}


bool DAGTriangleTree::getCoordIndices( TypeSet<int>& result ) const
{
    for ( int idx=triangles_.size()-1; idx>=0; idx-- )
    {
	const int* child = triangles_[idx].childindices_;
	if ( child[0]>=0 || child[1]>=0 || child[2]>=0 )
	    continue;

	const int* c = triangles_[idx].coordindices_;
	if ( c[0]<0 || c[1]<0 || c[2]<0 )
	    continue;
	
	result += c[0];
	result += c[1];
	result += c[2];
    }

    return result.size();
}


bool DAGTriangleTree::getConnections( int vertex, TypeSet<int>& result ) const
{
    result.erase();
    for ( int idx=triangles_.size()-1; idx>=0; idx-- )
    {
	const int* child = triangles_[idx].childindices_;
	if ( child[0]>=0 || child[1]>=0 || child[2]>=0 )
	    continue;

	const int* c = triangles_[idx].coordindices_;
	if ( c[0]!=vertex && c[1]!=vertex && c[2]!=vertex )
	    continue;

	for ( int idy=0; idy<3; idy++ )
	{
	    if ( c[idy]<0 || c[idy]==vertex || result.indexOf(c[idy])!=-1 )
		continue;

	    result += c[idy];
	}
    }

    return result.size();
}


void DAGTriangleTree::dumpTo(std::ostream& stream) const
{
    for ( int idx=0; idx<triangles_.size(); idx++ )
    {
	const DAGTriangle& triangle = triangles_[idx];
	stream << (int) triangle.coordindices_[0] << '\t'
	       << (int) triangle.coordindices_[1] << '\t'
	       << (int) triangle.coordindices_[2] << '\n';
    }
}


#define mSearch( child ) \
if ( child!=cNoTriangle() ) \
{ \
    const int* gc = triangles_[child].coordindices_; \
    if ( gc[0]==v0 && gc[1]==v1 || gc[1]==v0 && gc[0]==v1 || \
	 gc[0]==v0 && gc[2]==v1 || gc[2]==v0 && gc[0]==v1 || \
	 gc[1]==v0 && gc[2]==v1 || gc[2]==v0 && gc[1]==v1 )  \
    { \
	const int res = searchChild( v0, v1, child ); \
	return res; \
    } \
}


int DAGTriangleTree::searchChild( int v0, int v1, int ti ) const
{
    if ( ti==cNoTriangle() )
	return cError();

    rwlock_.readLock();
    const int* cptr = triangles_[ti].childindices_;
    const int children[] = { cptr[0], cptr[1], cptr[2] };
    rwlock_.readUnLock();

    if ( children[0]==cNoTriangle() && children[1]==cNoTriangle() &&
	 children[2]==cNoTriangle() )
	return ti;

    for ( int idx=0; idx<3; idx++ )
	mSearch( children[idx] );

    return cError();
}


DAGTriangleTree::DAGTriangle::DAGTriangle()
{
    coordindices_[0] = DAGTriangleTree::cNoVertex();
    coordindices_[1] = DAGTriangleTree::cNoVertex();
    coordindices_[2] = DAGTriangleTree::cNoVertex();
    childindices_[0] = DAGTriangleTree::cNoTriangle();
    childindices_[1] = DAGTriangleTree::cNoTriangle();
    childindices_[2] = DAGTriangleTree::cNoTriangle();
    neighbors_[0] = DAGTriangleTree::cNoTriangle();
    neighbors_[1] = DAGTriangleTree::cNoTriangle();
    neighbors_[2] = DAGTriangleTree::cNoTriangle();
}


char DAGTriangleTree::getCommonEdge( int ti0, int ti1 ) const
{
    const int* crds0 = triangles_[ti0].coordindices_;
    const int* crds1 = triangles_[ti1].coordindices_;
    for ( int idx=0; idx<3; idx++ )
    {
	if ( crds0[idx]!=crds1[0] && crds0[idx]!=crds1[1] && 
	     crds0[idx]!=crds1[2] )
	{
	    if ( idx==0 )
		return 1;
	    else if ( idx==1)
		return 2;
	    else
		return 0;
	}
    }

    return cNoTriangle();
}


bool DAGTriangleTree::DAGTriangle::hasChildren() const
{
    return childindices_[0]!=-1 || childindices_[1]!=-1 ||childindices_[2]!=-1;
}


bool DAGTriangleTree::DAGTriangle::operator==( 
	const DAGTriangle& dag ) const
{
    const int d0 = dag.coordindices_[0];
    const int d1 = dag.coordindices_[1];
    const int d2 = dag.coordindices_[2];
    return d0==coordindices_[0] && d1==coordindices_[1] && d2==coordindices_[2]
	|| d0==coordindices_[0] && d1==coordindices_[2] && d2==coordindices_[1]
	|| d0==coordindices_[1] && d1==coordindices_[0] && d2==coordindices_[2] 	|| d0==coordindices_[1] && d1==coordindices_[2] && d2==coordindices_[0] 	|| d0==coordindices_[2] && d1==coordindices_[1] && d2==coordindices_[0] 	|| d0==coordindices_[2] && d1==coordindices_[0] && d2==coordindices_[1];
}


DAGTriangleTree::DAGTriangle&
DAGTriangleTree::DAGTriangle::operator=( const DAGTriangleTree::DAGTriangle& b )
{
    for ( int idx=0; idx<3; idx++ )
    {
	coordindices_[idx] = b.coordindices_[idx];
	childindices_[idx] = b.childindices_[idx];
	neighbors_[idx] = b.neighbors_[idx];
    }

    return *this;
}
