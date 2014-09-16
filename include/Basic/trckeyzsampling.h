#ifndef trckeyzsampling_h
#define trckeyzsampling_h

/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:        Bert
 Date:          Feb 2002
 RCS:           $Id$
________________________________________________________________________

-*/

#include "basicmod.h"
#include "trckeysampling.h"


/*!
\brief Hor+Vert sampling in 3D surveys.

  When slices are to be taken from a TrcKeyZSampling, they should be ordered
  as follows:

  Dir |   Dim1  |  Dim2
  ----|---------|------
  Inl |   Crl   |  Z
  Crl |   Inl   |  Z
  Z   |   Inl   |  Crl

  See also the direction() and dimension() free functions.
*/

mExpClass(Basic) TrcKeyZSampling
{
public:

			TrcKeyZSampling();
			TrcKeyZSampling(const TrcKeyZSampling&);
			TrcKeyZSampling(bool settoSI);

    enum Dir		{ Z, Inl, Crl };
    Dir			defaultDir() const;
			//!< 'flattest' direction, i.e. direction with
			//!< smallest size. If equal, prefer Inl then Crl then Z
    void		getDefaultNormal(Coord3&) const;
    bool		isFlat() const; //!< is one of directions size 1?

    void		init(bool settoSI=true);
			//!< Sets hrg_.init and zrg_ to survey values or zeros
    inline void		setEmpty()		{ init(false); }
    void		set2DDef();
			//!< Sets to survey zrange and
    void		normalise();
			//!< Makes sure start<stop and steps are non-zero

    TrcKeySampling	hsamp_;
    StepInterval<float> zsamp_;

    int			lineIdx(Pos::LineID) const;
    int			trcIdx(Pos::TraceID) const;
    int			zIdx(float z) const;
    int			nrLines() const;
    int			nrTrcs() const;
    int			nrZ() const;
    od_int64		totalNr() const;
    int			size(Dir d) const;
    float		zAtIndex( int idx ) const;
    bool		isEmpty() const;
    bool		isDefined() const;
    bool		includes(const TrcKeyZSampling&) const;
    bool		getIntersection(const TrcKeyZSampling&,
					TrcKeyZSampling&) const;
			//!< Returns false if intersection is empty
    void		include(const TrcKey&,float z);
    void		include(const TrcKeyZSampling&);
    void		limitTo(const TrcKeyZSampling&,bool ignoresteps=false);
    void		limitToWithUdf(const TrcKeyZSampling&);
			/*!< handles undef values + returns reference cube
			     nearest limit if the 2 cubes do not intersect */

    void		snapToSurvey();
			/*!< Checks if it is on valid bids and sample positions.
			     If not, it will expand until it is */

    bool		operator==(const TrcKeyZSampling&) const;
    bool		operator!=(const TrcKeyZSampling&) const;
    TrcKeyZSampling&	operator=(const TrcKeyZSampling&);

    bool		usePar(const IOPar&);
    void		fillPar(IOPar&) const;
    static void		removeInfo(IOPar&);

//Legacy, don't use
    inline int		inlIdx( int inl ) const { return lineIdx(inl); }
    inline int		crlIdx( int crl ) const { return trcIdx(crl); }
    void		include(const BinID& bid,float z);

    inline int		nrInl() const		{ return nrLines(); }
    inline int		nrCrl() const		{ return nrTrcs(); }

    TrcKeySampling&		hrg;
    StepInterval<float>&	zrg;
};



mExpClass(Basic) TrcKeyZSamplingSet : public TypeSet<TrcKeyZSampling>
{

};


inline TrcKeyZSampling::Dir direction( TrcKeyZSampling::Dir slctype, int dimnr )
{
    if ( dimnr == 0 )
	return slctype;
    else if ( dimnr == 1 )
	return slctype == TrcKeyZSampling::Inl ? TrcKeyZSampling::Crl
					    : TrcKeyZSampling::Inl;
    else
	return slctype == TrcKeyZSampling::Z
		? TrcKeyZSampling::Crl
		: TrcKeyZSampling::Z;
}


inline int dimension( TrcKeyZSampling::Dir slctype,
		      TrcKeyZSampling::Dir direction )
{
    if ( slctype == direction )
	return 0;

    else if ( direction == TrcKeyZSampling::Z )
	return 2;
    else if ( direction == TrcKeyZSampling::Inl )
	return 1;

    return slctype == TrcKeyZSampling::Z ? 2 : 1;
}


typedef TrcKeyZSampling CubeSampling;


#endif

