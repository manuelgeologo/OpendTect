#ifndef attribstorprovider_h
#define attribstorprovider_h

/*+
________________________________________________________________________

 CopyRight:     (C) dGB Beheer B.V.
 Author:        Kristofer Tingdahl
 Date:          07-10-1999
 RCS:           $Id: attribstorprovider.h,v 1.19 2007-01-04 15:29:26 cvshelene Exp $
________________________________________________________________________

-*/

#include "attribprovider.h"
#include "cubesampling.h"
#include "seisreq.h"
#include "datachar.h"

namespace Attrib
{

class DataHolder;

class StorageProvider : public Provider
{
public:
    static void		initClass();
    static const char*  attribName()		{ return "Storage"; }
    static const char*  keyStr()		{ return "id"; }

    int			moveToNextTrace(BinID startpos = BinID(-1,-1),
	    				bool fisrcheck = false);
    bool		getPossibleVolume(int outp,CubeSampling&);
    BinID		getStepoutStep() const;
    void		updateStorageReqs(bool all=true);
    void		adjust2DLineStoredVolume(bool adjuststep = false);
    void		fillDataCubesWithTrc(DataCubes*) const;

protected:
    			StorageProvider(Desc&);
    			~StorageProvider();

    static Provider*	createFunc(Desc&);
    static void		updateDesc(Desc&);

    bool		init();
    bool		allowParallelComputation() const { return false; }

    SeisRequester*	getSeisRequester() const;
    bool		initSeisRequester(int req);
    bool		setSeisRequesterSelection(int req);

    void		setBufferStepout(const BinID&);
    bool        	computeData(const DataHolder& output,
				    const BinID& relpos,
				    int t0,int nrsamples) const;

    bool		fillDataHolderWithTrc(const SeisTrc*,
					      const DataHolder&) const;
    bool		getZStepStoredData(float& step) const
			{ step = storedvolume.zrg.step; return true; }

    BinDataDesc		getOutputFormat(int output) const;
    
    bool 		checkDataOK( StepInterval<int> trcrg,
	                             StepInterval<float>zrg );
    bool 		checkDataOK();

    TypeSet<BinDataDesc> datachar_;
    SeisReqGroup	rg;
    int			currentreq;

    CubeSampling	storedvolume;

    enum Status        { Nada, StorageOpened, Ready } status;
};

}; // namespace Attrib

#endif
