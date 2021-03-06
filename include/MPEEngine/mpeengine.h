#pragma once

/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:        Bert
 Date:          23-10-1996
 RCS:           $Id: mpeengine.h 38753 2015-04-11 21:19:18Z nanne.hemstra@dgbes.com $
________________________________________________________________________

-*/

#include "mpeenginemod.h"

#include "attribsel.h"
#include "notify.h"
#include "datapack.h"
#include "survgeom.h"
#include "trckeyzsampling.h"

class BufferStringSet;
class Executor;
class TaskRunnerProvider;

namespace EM { class Object; }
namespace Geometry { class Element; }
template <class T> class Selector;

namespace MPE
{

class EMTracker;
class HorizonTrackerMgr;
class ObjectEditor;

mExpClass(MPEEngine) TrackSettingsValidator
{
public:
    virtual		~TrackSettingsValidator()			{}
    virtual bool	checkInVolumeTrackMode() const			= 0;
    virtual bool	checkActiveTracker() const			= 0;
    virtual bool	checkStoredData(Attrib::SelSpec&,DBKey&) const = 0;
    virtual bool	checkPreloadedData(const DBKey&) const	= 0;
};


/*!
\brief Main engine for tracking EM objects like horizons, faults etc.,
*/

mExpClass(MPEEngine) Engine : public CallBacker
{ mODTextTranslationClass(Engine)
    mGlobal(MPEEngine) friend Engine&		engine();

public:
				Engine();
    virtual			~Engine();

    void			init();

    const TrcKeyZSampling&	activeVolume() const;
    void			setActiveVolume(const TrcKeyZSampling&);
    const TrcKeyPath*		activePath() const
				{ return rdmlinetkpath_; }
    void			setActivePath( const TrcKeyPath* tkp )
				{ rdmlinetkpath_ = tkp; }
    int				activeRandomLineID() const
				{ return rdlid_; }
    void			setActiveRandomLineID(  int rdlid )
				{ rdlid_ = rdlid; }
    Notifier<Engine>		activevolumechange;

    void			setActive2DLine(Pos::GeomID);
    Pos::GeomID			activeGeomID() const;

    Notifier<Engine>		loadEMObject;
    DBKey			emidtoload_;

    void			updateSeedOnlyPropagation(bool);

    enum TrackState		{ Started, Paused, Stopped };
    TrackState			getState() const	{ return state_; }
    bool			startTracking(uiString&);
    bool			startRetrack(uiString&);
    void			stopTracking();
    bool			trackingInProgress() const;
    void			undo(uiString& errmsg);
    void			redo(uiString& errmsg);
    bool			canUnDo();
    bool			canReDo();
    void			enableTracking(bool yn);
    Notifier<Engine>		actionCalled;
    Notifier<Engine>		actionFinished;

    void			removeSelectionInPolygon(
					const Selector<Coord3>&,
					const TaskRunnerProvider&);
    void			getAvailableTrackerTypes(BufferStringSet&)const;

    int				nrTrackersAlive() const;
    int				highestTrackerID() const;
    const EMTracker*		getTracker(int idx) const;
    EMTracker*			getTracker(int idx);
    int				getTrackerByObject(const DBKey&) const;
    int				getTrackerByObject(const char*) const;
    int				addTracker(EM::Object*);
    void			removeTracker(int idx);
    void			refTracker(const DBKey&);
    void			unRefTracker(const DBKey&,bool nodel=false);
    bool			hasTracker(const DBKey&) const;
    Notifier<Engine>		trackeraddremove;
    CNotifier<Engine,int>	trackertoberemoved;
    void			setActiveTracker(const DBKey&);
    void			setActiveTracker(EMTracker*);
    EMTracker*			getActiveTracker();

				/*Attribute stuff */
    void			setOneActiveTracker(const EMTracker*);
    void			unsetOneActiveTracker();
    void			getNeededAttribs(Attrib::SelSpecList&) const;
    TrcKeyZSampling		getAttribCube(const Attrib::SelSpec&) const;
				/*!< Returns the cube that is needed for
				     this attrib, given that the activearea
				     should be tracked. */
    int				getCacheIndexOf(const Attrib::SelSpec&) const;
    DataPack::ID		getAttribCacheID(const Attrib::SelSpec&) const;
    bool			hasAttribCache(const Attrib::SelSpec&) const;
    bool			setAttribData( const Attrib::SelSpec&,
					       DataPack::ID);
    bool			cacheIncludes(const Attrib::SelSpec&,
					      const TrcKeyZSampling&);
    void			swapCacheAndItsBackup();

    bool			pickingOnSameData(const Attrib::SelSpec& oldss,
						  const Attrib::SelSpec& newss,
						  uiString& error) const;
    bool			isSelSpecSame(const Attrib::SelSpec& setupss,
					const Attrib::SelSpec& clickedss) const;

    void			updateFlatCubesContainer(const TrcKeyZSampling&,
							 int idx,bool);
				/*!< add = true, remove = false. */
    ObjectSet<TrcKeyZSampling>* getTrackedFlatCubes(const int idx) const;
    DataPack::ID		getSeedPosDataPack(const TrcKey&,float z,
					int nrtrcs,
					const StepInterval<float>& zrg) const;

				/*Editors */
    ObjectEditor*		getEditor(const DBKey&,bool create);
    void			removeEditor(const DBKey&);

    void			setValidator(TrackSettingsValidator*);
    const char*			errMsg() const;

    BufferString		setupFileName(const DBKey&) const;

    void			fillPar(IOPar&) const;
    bool			usePar(const IOPar&);

    Notifier<Engine>		settingsChanged;

protected:
    void			applClosingCB(CallBacker*);

    TrackSettingsValidator*	validator_;
    BufferString		errmsg_;
    TrcKeyZSampling		activevolume_;
    const TrcKeyPath*		rdmlinetkpath_;
    int				rdlid_;

    Pos::GeomID			activegeomid_;

    TrackState			state_;
    ObjectSet<HorizonTrackerMgr> trackermgrs_;
    ObjectSet<EMTracker>	trackers_;
    ObjectSet<ObjectEditor>	editors_;

    const EMTracker*		oneactivetracker_;
    EMTracker*			activetracker_;
    int				undoeventid_;
    DataPackMgr&		dpm_;

    bool			prepareForTrackInVolume(uiString&);
    bool			prepareForRetrack();
    bool			trackInVolume();
    void			trackingFinishedCB(CallBacker*);
    EM::Object*			getCurrentEMObject() const;

    struct CacheSpecs
    {
				CacheSpecs(const Attrib::SelSpec& as,
					Pos::GeomID geomid=Pos::GeomID())
				    : attrsel_(as),geomid_(geomid)
				{}

	Attrib::SelSpec		attrsel_;
	Pos::GeomID		geomid_;
    };

    TypeSet<DataPack::ID>	attribcachedatapackids_;
    ObjectSet<CacheSpecs>	attribcachespecs_;
    TypeSet<DataPack::ID>	attribbkpcachedatapackids_;
    ObjectSet<CacheSpecs>	attribbackupcachespecs_;

    mStruct(MPEEngine) FlatCubeInfo
    {
				FlatCubeInfo()
				:nrseeds_(1)
				{
				    flatcs_.setEmpty();
				}
	TrcKeyZSampling		flatcs_;
	int			nrseeds_;
    };

    ObjectSet<ObjectSet<FlatCubeInfo> >	flatcubescontainer_;

    static const char*		sKeyNrTrackers(){ return "Nr Trackers"; }
    static const char*		sKeyObjectID()	{ return "ObjectID"; }
    static const char*		sKeyEnabled()	{ return "Is enabled"; }
    static const char*		sKeyTrackPlane(){ return "Track Plane"; }
    static const char*		sKeySeedConMode(){ return "Seed Connect Mode"; }
};


mGlobal(MPEEngine) Engine&	engine();

} // namespace MPE
