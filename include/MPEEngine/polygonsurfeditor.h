#pragma once

/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:        Yuancheng Liu
 Date:          July 2008
________________________________________________________________________

-*/

#include "mpeenginemod.h"
#include "emeditor.h"

namespace EM { class PolygonBody; };
template <class T> class Selector;

namespace MPE
{

/*!
\brief ObjectEditor to edit EM::PolygonBody.
*/

mExpClass(MPEEngine) PolygonBodyEditor : public ObjectEditor
{
public:
				PolygonBodyEditor(EM::PolygonBody&);
    static ObjectEditor*	create(EM::Object&);
    static void			initClass();

    void			setLastClicked(const EM::PosID&);
    void			setSowingPivot(const Coord3);

    void			getInteractionInfo(EM::PosID& nearestpid0,
					   EM::PosID& nearestpid1,
					   EM::PosID& insertpid,
					   const Coord3&,float zfactor) const;
    bool			removeSelection(const Selector<Coord3>&);

protected:

    bool			setPosition(const EM::PosID&,const Coord3&);
    bool			setPosition( const Coord3& c )
				{ return ObjectEditor::setPosition(c); }
    Geometry::ElementEditor*	createEditor();
    float			getNearestPolygon(int& polygon,
						  const Coord3&,
						  float zfactor) const;
    void			getPidsOnPolygon(EM::PosID& nearestpid0,
						 EM::PosID& nearestpid1,
						 EM::PosID& insertpid,
						 int polygon,
						 const Coord3&,
						 float zfactor) const;

    Coord3			sowingpivot_;
    mutable TypeSet<Coord3>	sowinghistory_;
};


};  // namespace MPE
