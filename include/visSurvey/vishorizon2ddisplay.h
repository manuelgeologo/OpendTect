#ifndef vishorizon2ddisplay_h
#define vishorizon2ddisplay_h

/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:        Kristofer Tingdahl
 Date:          May 2004
 RCS:           $Id$
________________________________________________________________________


-*/

#include "vissurveymod.h"
#include "emposid.h"
#include "multiid.h"
#include "visemobjdisplay.h"

namespace visBase { class PolyLine3D; class PointSet;  }
namespace EM { class Horizon2D; }
class ZAxisTransform;

namespace visSurvey
{

class Seis2DDisplay;

mExpClass(visSurvey) Horizon2DDisplay : public EMObjectDisplay
{ mODTextTranslationClass(Horizon2DDisplay);
public:
				Horizon2DDisplay();
				mDefaultFactoryInstantiation(
				    visSurvey::SurveyObject,Horizon2DDisplay,
				    "Horizon2DDisplay", 
				    toUiString(sFactoryKeyword()));

    void			setDisplayTransformation(const mVisTrans*);

    void			getMousePosInfo(const visBase::EventInfo& e,
						IOPar& i ) const
				{ return EMObjectDisplay::getMousePosInfo(e,i);}
    virtual void		getMousePosInfo(const visBase::EventInfo&,
						Coord3&,
						BufferString& val,
						BufferString& info) const;
    void			setLineStyle(const LineStyle&);

    bool			setEMObject(const EM::ObjectID&,TaskRunner*);
    EM::SectionID		getSectionID(int visid) const;
    TypeSet<EM::SectionID>	getSectionIDs() const{ return sids_; }

    bool			setZAxisTransform(ZAxisTransform*,TaskRunner*);

    const visBase::PointSet*	getPointSet(const EM::SectionID&) const;
    const visBase::PolyLine3D*	getLine(const EM::SectionID&) const;
    void			doOtherObjectsMoved(
				    const ObjectSet<const SurveyObject>&,
				    int whichobj );
    virtual void		setPixelDensity(float);


protected:
    friend			class Horizon2DDisplayUpdater;
				~Horizon2DDisplay();
    void			removeSectionDisplay(const EM::SectionID&);
    bool			addSection(const EM::SectionID&,TaskRunner*);

    struct LineRanges
    {
	TypeSet<TypeSet<Interval<int> > >	trcrgs;
	TypeSet<TypeSet<Interval<float> > >	zrgs;
    };

    static bool			withinRanges(const RowCol&,float z,
					     const LineRanges& );
    void			updateSection(int idx,const LineRanges* lr=0);

    void			emChangeCB(CallBacker*);

    void			otherObjectsMoved(
				    const ObjectSet<const SurveyObject>&,
				    int whichobj );
    void			updateLinesOnSections(
					const ObjectSet<const Seis2DDisplay>&);
    void			updateSeedsOnSections(
					const ObjectSet<const Seis2DDisplay>&);

    void			zAxisTransformChg(CallBacker*);
    void			removeVolumesOfInterest();

    void			fillPar(IOPar&) const;
    bool			usePar(const IOPar&);

    ObjectSet<visBase::PolyLine3D>		lines_;
    ObjectSet<visBase::PointSet>		points_;
    TypeSet<EM::SectionID>			sids_;
    TypeSet<int>				volumeofinterestids_;
};

} // namespace visSurvey

#endif

