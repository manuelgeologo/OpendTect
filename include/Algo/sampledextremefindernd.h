#ifndef sampledextremefindernd_h 
#define sampledextremefindernd_h 

/*+
________________________________________________________________________

 CopyRight:     (C) dGB Beheer B.V.
 Author:        K. Tingdahl
 Date:          July 2008
 RCS:           $Id: sampledextremefindernd.h,v 1.1 2008-07-11 17:45:19 cvskris Exp $
________________________________________________________________________

-*/

#include "dataclipper.h"
#include "thread.h"

//!Finds all local maximas/minimas in an ArrayND

template <class T>
class SampledExtremeFinderND : public ParallelTask
{
public:
		SampledExtremeFinderND(const ArrayND<T>& arr, bool minima)
		    : array_( arr )
		    , minima_( minima )
		    , relcube_( arr.info().getNDim() )
		{
		    const int ndim = array_.info().getNDim();
		    for ( int idx=0; idx<ndim; idx++ )
		    relcube_.setSize( idx, 3 );
		}

    inline int	nrExtremes() const;

    const int*	getExtremes() const { return extremes_.arr(); }
		//!<Returns a pointer with positions, where
		//!<ptr[0] ... ptr[ndim-1] is one position.

protected:
    inline int	totalNr() const;
    inline bool	doWork(int,int,int);
    inline bool	findExtreme(int*) const;
    inline int	indexOf(const int*) const;

    const ArrayND<T>&		array_;
    ArrayNDInfoImpl		relcube_;
    bool			minima_;
    TypeSet<int>		extremes_;
    Threads::ReadWriteLock	lock_;
};


template <class T> inline
int SampledExtremeFinderND<T>::totalNr() const
{ return array_.info().getTotalSz(); }


template <class T> inline
int SampledExtremeFinderND<T>::nrExtremes() const
{
    const int ndim = array_.info().getNDim();
    return extremes_.size()/ndim;
}


template <class T> inline
bool SampledExtremeFinderND<T>::doWork( int start, int stop, int )
{
    const int ndim = array_.info().getNDim();
    mVariableLengthArr( int, pos, ndim );
    if ( !array_.info().getArrayPos( start, pos ) )
	return false;

    ArrayNDIter iter( array_.info() );
    iter.setPos<int*>( pos );
			    
    mVariableLengthArr( int, currentextreme, ndim );
    for ( int idx=start; idx<=stop && shouldContinue();
	  idx++, reportNrDone(1), iter.next() ) 
    {
	memcpy( currentextreme, iter.getPos(), ndim*sizeof(int) );
	if ( !findExtreme( currentextreme ) )
	    continue;

	lock_.readLock();

	int extremeidx = indexOf( currentextreme );
	if ( extremeidx!=-1 )
	{
	    lock_.readUnLock();
	    continue;
	}

	if ( !lock_.convToWriteLock() )
	{
	    extremeidx = indexOf( currentextreme );
	    if ( extremeidx!=-1 )
	    {
		lock_.writeUnLock();
		continue;
	    }
	}

	for ( int idy=0; idy<ndim; idy++ )
	    extremes_ += currentextreme[idy];

	lock_.writeUnLock();
    }

    return true;
}


template <class T> inline
int SampledExtremeFinderND<T>::indexOf( const int* pos ) const
{
    const int nrextremes = nrExtremes();
    const int ndim = array_.info().getNDim();

    for ( int idx=0; idx<nrextremes; idx++ )
    {
	const int* curpos = extremes_.arr() + idx*ndim;
	bool found = true;
	for ( int idy=0; idy<ndim; idy++ )
	{
	    if ( curpos[idy]!=pos[idy] )
	    {
		found = false;
		break;
	    }
	}

	if ( found )
	return idx;
    }

    return -1;
}


template <class T> inline
bool SampledExtremeFinderND<T>::findExtreme( int* extremepos ) const
{
    const int ndim = array_.info().getNDim();

    T extremeval = array_.get( extremepos );

    mVariableLengthArr( int, curpos, ndim );
    mVariableLengthArr( int, bestpos, ndim );
    memcpy( bestpos, extremepos, ndim*sizeof(int) );

    bool change = true;
    bool anychange = false;
    
    while ( change )
    {
	ArrayNDIter iter( relcube_ );
	change = false;
	do
	{
	    bool invalid = false;
	    bool isnull = true;
	    for ( int idx=0; idx<ndim; idx++ )
	    {
		if ( iter[idx] ) isnull = false;
		const int newpos = extremepos[idx]+iter[idx]-1;
		if ( newpos<0 || newpos>=array_.info().getSize(idx) )
		{
		    invalid = true;
		    break;
		}

		curpos[idx] = newpos;
	    }

	    if ( invalid || isnull )
	    continue;

	    const T val = array_.get( curpos );
	    if ( (minima_ && val<extremeval) || (!minima_ && val>extremeval) )
	    {
		memcpy( bestpos, curpos, ndim*sizeof(int) );
		extremeval = val;
		change = true;
	    }
	} while ( iter.next() );

	if ( change )
	{
	    memcpy( extremepos, bestpos, ndim*sizeof(int) );
	    anychange = true;
	}
    }

    return anychange;
}


#endif
