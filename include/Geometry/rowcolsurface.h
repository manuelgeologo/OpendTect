#ifndef rowcolsurface_h
#define rowcolsurface_h
                                                                                
/*+
________________________________________________________________________
CopyRight:     (C) dGB Beheer B.V.
Author:        K. Tingdahl
Date:          April 2006
Contents:      Ranges
RCS:           $Id: rowcolsurface.h,v 1.1 2006-04-18 17:47:40 cvskris Exp $
________________________________________________________________________

-*/

#include "rowcol.h"
#include "geomelement.h"

template <class T> class Array2D;

namespace Geometry
{

/*!Surface which positions are orgainzied in rows/cols. The number of
   columns in each row may vary. */

class RowColSurface : public Element
{
public:
    virtual void		getPosIDs(TypeSet<GeomPosID>&,bool=true) const;

    virtual StepInterval<int>	colRange() const;
    virtual StepInterval<int>	colRange(int row) const			= 0;
    virtual StepInterval<int>	rowRange() const			= 0;

    virtual bool		setKnot(const RCol&,const Coord3&)	= 0;
    virtual Coord3		getKnot(const RCol&) const		= 0;
    virtual bool		isKnotDefined(const RCol&) const	= 0;

    virtual Coord3		getPosition(GeomPosID pid) const;
    virtual bool		setPosition(GeomPosID pid,const Coord3&);
    virtual bool		isDefined(GeomPosID pid) const;
};

};

#endif
