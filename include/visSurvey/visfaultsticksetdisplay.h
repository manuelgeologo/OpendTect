#ifndef visfaultsticksetdisplay_h
#define visfaultsticksetdisplay_h

/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:	J.C. Glas
 Date:		November 2008
 RCS:		$Id$
________________________________________________________________________


-*/

#include "vissurveymod.h"
#include "vissurvobj.h"
#include "visobject.h"

#include "emposid.h"


namespace visBase
{
    class DrawStyle;
    class Lines;
    class MarkerSet;
    class Transformation;
}

namespace EM { class FaultStickSet; }
namespace Geometry { class FaultStickSet; class IndexedPrimitiveSet; }
namespace MPE { class FaultStickSetEditor; }

namespace visSurvey
{

class MPEEditor;
class Seis2DDisplay;

/*!\brief Display class for FaultStickSets
*/

mExpClass(visSurvey) FaultStickSetDisplay : public visBase::VisualObjectImpl
					  , public SurveyObject
{ mODTextTranslationClass(FaultStickSetDisplay);
public:
				FaultStickSetDisplay();

				mDefaultFactoryInstantiation(
				visSurvey::SurveyObject,
				FaultStickSetDisplay, 
				"FaultStickSetDisplay",
				toUiString(sFactoryKeyword()) );


    MultiID			getMultiID() const;
    bool			isInlCrl() const	{ return false; }

    bool			hasColor() const		{ return true; }
    Color			getColor() const;
    void			setColor(Color);
    bool			allowMaterialEdit() const	{ return true; }
    NotifierAccess*		materialChange();
    const LineStyle*		lineStyle() const;
    void			setLineStyle(const LineStyle&);

    void			hideAllKnots(bool yn);
    bool			areAllKnotsHidden() const;

    void			showManipulator(bool);
    bool			isManipulatorShown() const;

    void			setDisplayTransformation(const mVisTrans*);
    const mVisTrans*		getDisplayTransformation() const;

    void			setSceneEventCatcher(visBase::EventCatcher*);

    bool			setEMID(const EM::ObjectID&);
    EM::ObjectID		getEMID() const;

    const char*			errMsg() const { return errmsg_.str(); }

    void			updateSticks(bool activeonly=false);
    void			updateEditPids();
    void			updateKnotMarkers();
    void			updateAll();

    Notifier<FaultStickSetDisplay> colorchange;
    Notifier<FaultStickSetDisplay> displaymodechange;

    void			removeSelection(const Selector<Coord3>&,
						TaskRunner*);
    bool			canRemoveSelection() const	{ return true; }

    void			setDisplayOnlyAtSections(bool yn);
    bool			displayedOnlyAtSections() const;

    void			setStickSelectMode(bool yn);
    bool			isInStickSelectMode() const;

    bool			allowsPicks() const		{ return true; }

    void			getMousePosInfo(const visBase::EventInfo& ei,
						IOPar& iop ) const
				{ return SurveyObject::getMousePosInfo(ei,iop);}
    void			getMousePosInfo(const visBase::EventInfo&,
					Coord3& xyzpos,BufferString& val,
					BufferString& info) const;

    virtual void                fillPar(IOPar&) const;
    virtual bool                usePar(const IOPar&);
    virtual void		setPixelDensity(float dpi);

protected:
    virtual			~FaultStickSetDisplay();

    void			otherObjectsMoved(
					const ObjectSet<const SurveyObject>&,
					int whichobj);

    void			setActiveStick(const EM::PosID&);

    static const char*		sKeyEarthModelID();
    static const char*		sKeyDisplayOnlyAtSections();


    void			mouseCB(CallBacker*);
    void			draggingStartedCB(CallBacker*);
    void			stickSelectCB(CallBacker*);
    void			emChangeCB(CallBacker*);
    void			polygonFinishedCB(CallBacker*);
    bool			isSelectableMarkerInPolySel(
					const Coord3& markerworldpos) const;

    Coord3			disp2world(const Coord3& displaypos) const;

    void			displayOnlyAtSectionsUpdate();
    bool			coincidesWith2DLine(
					Geometry::FaultStickSet&,
					int sticknr,Pos::GeomID);
    bool			coincidesWithPlane(
					Geometry::FaultStickSet&,
					int sticknr,
					TypeSet<Coord3>& intersectpoints);

    visBase::EventCatcher*	eventcatcher_;
    const mVisTrans*		displaytransform_;

    EM::FaultStickSet*		emfss_;
    MPE::FaultStickSetEditor*	fsseditor_;
    visSurvey::MPEEditor*	viseditor_;

    Coord3			mousepos_;
    bool			showmanipulator_;

    int				activesticknr_;

    TypeSet<EM::PosID>		editpids_;

    visBase::Lines*		sticks_;
    visBase::Lines*		activestick_;
    visBase::DrawStyle*		stickdrawstyle_;
    visBase::DrawStyle*		activestickdrawstyle_;

    bool			displayonlyatsections_;
    bool			stickselectmode_;

    bool			ctrldown_;

    bool			hideallknots_;

    ObjectSet<visBase::MarkerSet>  knotmarkersets_;
    struct StickIntersectPoint
    {
	Coord3			pos_;
	int			sid_;
	int			sticknr_;
    };

    ObjectSet<StickIntersectPoint> stickintersectpoints_;
};

} // namespace VisSurvey

#endif

