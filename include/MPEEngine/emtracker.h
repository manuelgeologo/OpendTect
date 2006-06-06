#ifndef emtracker_h
#define emtracker_h
                                                                                
/*+
________________________________________________________________________

 CopyRight:     (C) dGB Beheer B.V.
 Author:        A.H. Bril
 Date:          23-10-1996
 RCS:           $Id: emtracker.h,v 1.24 2006-06-06 14:13:13 cvsjaap Exp $
________________________________________________________________________

-*/

#include "sets.h"
#include "emposid.h"
#include "cubesampling.h"

class Executor;

namespace Geometry { class Element; };
namespace EM { class EMObject; };
namespace Attrib { class SelSpec; }

namespace MPE
{

class ConsistencyChecker;
class SectionTracker;
class TrackPlane;
class EMSeedPicker;

class EMTracker
{
public:
    				EMTracker( EM::EMObject* );
    virtual			~EMTracker();

    BufferString		objectName() const;
    EM::ObjectID		objectID() const;

    virtual bool		is2D() const		{ return false; }

    virtual bool		isEnabled() const	{ return isenabled_; }
    virtual void		enable(bool yn)		{ isenabled_=yn; }

    virtual bool		trackSections(const TrackPlane&);
    virtual bool		trackIntersections(const TrackPlane&);
    virtual Executor*		trackInVolume();
    virtual bool		snapPositions( const TypeSet<EM::PosID>& );

    virtual CubeSampling	getAttribCube( const Attrib::SelSpec& ) const;
    void			getNeededAttribs(
	    				ObjectSet<const Attrib::SelSpec>& );

    SectionTracker*		getSectionTracker(EM::SectionID,
	    					  bool create=false);
    virtual EMSeedPicker*	getSeedPicker(bool createifnotpresent=true)
				{ return 0; }
    void 			applySetupAsDefault(const EM::SectionID);

    const char*			errMsg() const;

    void			fillPar(IOPar&) const;
    bool			usePar(const IOPar&);

protected:
    virtual SectionTracker*	createSectionTracker(EM::SectionID) = 0;
    virtual void		erasePositions(EM::SectionID,
	    				       const TypeSet<EM::SubID>&);
    virtual ConsistencyChecker*	getConsistencyChecker()		{ return 0; }

    bool			isenabled_;
    ObjectSet<SectionTracker>	sectiontrackers_;
    BufferString		errmsg_;

    EM::EMObject*		emObject()      { return emobject_; }
    void			setEMObject(EM::EMObject*);

    static const char*		setupidStr()	{ return "SetupID"; }
    static const char*		sectionidStr()	{ return "SectionID"; }

private:
    EM::EMObject*		emobject_;
};


typedef EMTracker*(*EMTrackerCreationFunc)(EM::EMObject*);


class TrackerFactory
{
public:
			TrackerFactory(const char* emtype,
				       EMTrackerCreationFunc);
    const char*		emObjectType() const;
    EMTracker*		create(EM::EMObject* emobj=0) const;

protected:
    EMTrackerCreationFunc	createfunc;
    const char*			type;

};

}; // namespace MPE

#endif

