#ifndef property_h
#define property_h

/*+
________________________________________________________________________

 CopyRight:	(C) dGB Beheer B.V.
 Author:	Bert Bril
 Date:		Dec 2003
 RCS:		$Id: property.h,v 1.1 2004-01-05 14:48:28 bert Exp $
________________________________________________________________________


-*/

#include "uidobj.h"
#include "enums.h"
class MeasureUnit;

/*!\brief Ref Data for a (usually petrophysical) property */

class PropertyRef : public ::UserIDObject
{
public:

    enum StdType		{ Udef, Pson, Den, AI, Por, Perm, Sw,
				  Sson, Pvel, Svel, GR, SP, PR,
				  Res, Comp, Cal, Temp, Pres };
				DeclareEnumUtilsWithVar(StdType,stdtype)

				PropertyRef( const char* nm=0 )
				: UserIDObject(nm)
				, stdtype_(Udef)	{}

    ObjectSet<MeasureUnit>&	measUnits()		{ return measunits_; }
    const ObjectSet<MeasureUnit>& measUnits() const	{ return measunits_; }

protected:

    ObjectSet<MeasureUnit>	measunits_;

};


/*!\brief A (usually petrophysical) property of some object */

class Property
{
public:

    			Property(const PropertyRef* pr )
			: ref_(pr)		{}
    virtual		~Property()		{}

    virtual float	value() const		= 0;
    const PropertyRef*	ref() const		{ return ref_; }

    virtual bool	dependsOn(const Property*) const { return false; }

protected:

    const PropertyRef*	ref_;

};


#endif
