/*+
________________________________________________________________________

 CopyRight:     (C) dGB Beheer B.V.
 Author:        H.Payraudeau
 Date:          04/2005
 RCS:           $Id: attribengman.cc,v 1.15 2005-08-18 14:19:21 cvsnanne Exp $
________________________________________________________________________

-*/

#include "attribengman.h"
#include "attribprocessor.h"
#include "attriboutput.h"
#include "attribprovider.h"
#include "attribdescset.h"
#include "attribdesc.h"
#include "attribfactory.h"
#include "attribslice.h"
#include "attribsel.h"
#include "attribstorprovider.h"
#include "attribparam.h"
#include "survinfo.h"
#include "ptrman.h"
#include "cubesampling.h"
#include "separstr.h"
#include "nlamodel.h"
#include "iopar.h"
#include "ioman.h"
#include "ioobj.h"
#include "linekey.h"



namespace Attrib
{
    
EngineMan::EngineMan()
	: inpattrset(0)
	, procattrset(0)
	, nlamodel(0)
	, cs_(*new CubeSampling)
	, attrspec(*new SelSpec)
	, cache(0)
	, udfval( mUdf(float) )
	, curattrid(DescID::undef())
{
}


EngineMan::~EngineMan()
{
    delete procattrset;
    delete inpattrset;
    delete nlamodel;
    delete &cs_;
    delete &attrspec;
    if ( cache ) cache->unRef();
}


void EngineMan::getPossibleVolume( const DescSet& attribset, CubeSampling& cs,
				   const char* linename, const DescID& outid )
{
    ObjectSet<Processor> pset;
    TypeSet<DescID> desiredids;
    desiredids += outid;
    createProcSet( pset, attribset, linename, desiredids );
    if ( !pset.size() ) return;

    pset[0]->getProvider()->setDesiredVolume( cs );
    pset[0]->getProvider()->getPossibleVolume( -1, cs );
    deepErase( pset );
}


void EngineMan::usePar( const IOPar& iopar, const DescSet& attribset, 
	      		const char* linename, ObjectSet<Processor>& pset )
{
    int outputidx = 0;
    TypeSet<DescID> ids;
    while ( true )
    {    
	BufferString outpstr = IOPar::compKey( "Output", outputidx );
	PtrMan<IOPar> outputpar = iopar.subselect( outpstr );
	if ( !outputpar )
	{
	    if ( !outputidx )
	    { outputidx++; continue; }
	    else 
		break;
	}

	int attribidx = 0;
	while ( true )
	{
	    BufferString attribidstr = 
			IOPar::compKey( "Attributes", attribidx );
	    int attribid;
	    if ( !outputpar->get(attribidstr,attribid) )
		break;

	    ids += DescID(attribid,true);
	    attribidx++;
	}

	outputidx++;
    }

    createProcSet( pset, attribset, linename, ids );

    iopar.get( "Output.1.In-line range", cs_.hrg.start.inl, cs_.hrg.stop.inl );
    iopar.get( "Output.1.Cross-line range",cs_.hrg.start.crl, cs_.hrg.stop.crl);
    iopar.get( "Output.1.Depth range", cs_.zrg.start, cs_.zrg.stop );
    cs_.zrg.start /= SI().zFactor();
    cs_.zrg.stop /= SI().zFactor();

    LineKey lkey( linename, attribset.getDesc(ids[0])->attribName() );
    CubeOutput* cubeoutp = createOutput( iopar, lkey );
    for ( int idx=0; idx<pset.size(); idx++ )
	pset[idx]->addOutput( cubeoutp );
}


void EngineMan::createProcSet( ObjectSet<Processor>& pset,
			       const DescSet& attribset,
			       const char* linename,
			       const TypeSet<DescID>& outids )
{
    ObjectSet<Desc> targetdescset;
    if ( !outids.size() ) return;
    
    Desc* targetdesc = const_cast<Desc*>(attribset.getDesc(outids[0]));
    if ( !targetdesc ) return;
    targetdescset += targetdesc;
    
    Processor* processor = new Processor( *targetdesc, linename );
    if ( !processor->isOK() )
    {
	delete processor;
	return;
    }

    processor->addOutputInterest( targetdesc->selectedOutput() );
    pset += processor;

    for ( int index=1; index<outids.size(); index++ )
    {
	Desc* candidate = const_cast<Desc*>(attribset.getDesc(outids[index]));
	if ( candidate )
	{
	    bool identical = false;
	    for ( int idx=0; idx<targetdescset.size(); idx++ )
	    {
		if ( candidate->isIdenticalTo( *targetdescset[idx], false ) )
		{
		    const int output_cand = candidate->selectedOutput();
		    if ( targetdescset[idx]->selectedOutput() != output_cand )
			pset[idx]->addOutputInterest( output_cand );
		    identical = true;
		    break;
		}
	    }

	    if ( !identical )
	    {
		Processor* proc = new Processor( *candidate, linename );
		proc->addOutputInterest( candidate->selectedOutput() );
		targetdescset += candidate;
		pset += proc;
	    }
	}
    }
}


CubeOutput* EngineMan::createOutput( const IOPar& pars, const LineKey& lkey )
{
    const char* typestr = pars.find("Output.1.Type");

    if ( !strcmp(typestr,"Cube") )
    {
	CubeOutput* outp = new CubeOutput( cs_, lkey );
	outp->doUsePar( pars );
	return outp;
    }

    return 0;
}


void EngineMan::setNLAModel( const NLAModel* m )
{
    delete nlamodel;
    nlamodel = m ? m->clone() : 0;
}


void EngineMan::setAttribSet( const DescSet* ads )
{
    delete inpattrset;
    inpattrset = ads ? ads->clone() : 0;
}


const char* EngineMan::curUserDesc() const
{
    if ( attrspec.id() < 0 ) return "";

    SelSpec& ss = const_cast<EngineMan*>(this)->attrspec;
    if ( attrspec.isNLA() )
    {
	if ( !nlamodel ) return "";
	ss.setRefFromID( *nlamodel );
    }
    else
    {
	if ( !inpattrset ) return "";
	ss.setRefFromID( *inpattrset );
    }
    return attrspec.userRef();
}


SliceSet* EngineMan::getSliceSetOutput()
{
    if ( !procset.size() )
	return 0;

    if ( procset.size() == 1 && procset[0]->outputs.size() == 1 && !cache )
	return procset[0]->outputs[0]->getSliceSet();

    ObjectSet<SliceSet> slsets;
    for ( int idx=0; idx<procset[0]->outputs.size(); idx++ )
    {
	SliceSet& slset = *new SliceSet;
	slset.sampling = procset[0]->outputs[idx]->getSliceSet()->sampling;
	slset.direction = slset.sampling.defaultDir();
	for ( int idy=0; idy<procset.size(); idy++ )
	{
	    if ( procset[idy]->outputs[idx]->getSliceSet()->size()>1 )
		return 0;
	    else
	    {
		Slice* slc = (*procset[idy]->outputs[idx]->getSliceSet())[0];
		slset += slc;
	    }
	}

	slset.ref();
	slsets += &slset;
    }

    bool prevismine = false;
    if ( !cache )
    {
	cache = slsets[0];
	slsets.remove(0);
	prevismine = true;
    }
    
    SliceSet* outslcs = new SliceSet;
    CubeSampling csamp(cs_);
    outslcs->sampling = csamp;
    outslcs->direction = csamp.defaultDir();
#define mGetDim(nr) \
    const int dim##nr = csamp.size( direction(outslcs->direction,nr) )

    mGetDim(0); mGetDim(1); mGetDim(2);
    float undfval;
    bool udefvalfound = false;
    for ( int idx=0; idx<cache->size(); idx++ )
    {
	if ( (*cache)[idx] )
	{
	    udefvalfound = true;
	    undfval= (*cache)[idx]->undefValue();
	    break;
	}
    }
    if ( !udefvalfound )
    {
	for ( int idy=0; idy<slsets.size(); idy++ )
	{
	    if ( !slsets[idy] ) continue;
	    for ( int idx=0; idx<slsets[idy]->size(); idx++ )
	    {
		if ( (*slsets[idy])[idx] )
		{
		    udefvalfound = true;
		    undfval= (*slsets[idy])[idx]->undefValue();
		    break;
		}
	    }

	    if ( udefvalfound ) break;
	}
    }

    if ( !udefvalfound )
	undfval = mUndefValue;

    for ( int idx=0; idx<dim0; idx++ )
	*outslcs += new Slice( dim1, dim2, udfval );

    int i0, i1, i2;
    for ( int iset=-1; iset<slsets.size(); iset++ )
    {
	const SliceSet& slset = *(iset < 0 ? cache : slsets[iset]);

	CubeSampling datacs( slset.sampling );
#define mCheckRg(memb,op) \
	if ( datacs.memb op outslcs->sampling.memb ) \
	    datacs.memb = outslcs->sampling.memb
	mCheckRg(hrg.start.inl,<); mCheckRg(hrg.stop.inl,>);
	mCheckRg(hrg.start.crl,<); mCheckRg(hrg.stop.crl,>);
	mCheckRg(zrg.start,<); mCheckRg(zrg.stop,>);

	BinID bid;
	const float hzstep = outslcs->sampling.zrg.step * .5;
	for ( int inl =  datacs.hrg.start.inl;
		  inl <= datacs.hrg.stop.inl;
		    inl += datacs.hrg.step.inl )
	{
	    for ( int   crl =  datacs.hrg.start.crl;
			crl <= datacs.hrg.stop.crl;
			crl += datacs.hrg.step.crl )
	    {
		for ( float z =  datacs.zrg.start;
			    z <  datacs.zrg.stop + hzstep;
			    z += datacs.zrg.step )
		{
		    slset.getIdxs( inl, crl, z, i0, i1, i2 );
		    float val = slset[i0] ? slset[i0]->get( i1, i2 ) : udfval;
		    outslcs->getIdxs( inl, crl, z, i0, i1, i2 );
		    ((*outslcs)[i0])->set( i1, i2, val );
		}
	    }
	}
    }

    for ( int idx=0; idx<slsets.size(); idx++ )
	slsets[idx]->unRef();

    return outslcs;
}


SeisTrcBuf* EngineMan::get2DLineOutput()
{
    if ( !procset.size() )
	return 0;

    if ( !procset[0]->outputs[0] ) return 0;

    return procset[0]->outputs[0]->getTrcBuf();
}


void EngineMan::setAttribSpec( const SelSpec& a )
{
    attrspec = a;
}


void EngineMan::setCubeSampling( const CubeSampling& newcs )
{
    cs_ = newcs;
    cs_.normalise();
}


void EngineMan::addOutputAttrib( const DescID& id )
{
    outattribs += id;
}


#define mErrRet() \
	delete ads; desc->unRef(); return 0

DescSet* EngineMan::createNLAADS( DescID& outpid, BufferString& errmsg,
       				  const DescSet* addtoset )
{
    DescSet* ads = addtoset ? addtoset->clone() : new DescSet;
    if ( !addtoset && !ads->usePar(const_cast<NLAModel*>(nlamodel)->pars()) )
    {
	errmsg = ads->errMsg();
	delete ads;
	return 0;
    }

    BufferString s;
    nlamodel->dump(s);
    Desc* desc = PF().createDescCopy( "NN" );
    desc->setDescSet( ads );
    BufferString def( nlamodel->nlaType(true) );
    def += " specification=\""; def += s; def += "\"";

    if ( !desc->parseDefStr(def.buf()) )
    { 
	errmsg = "cannot parse definition string"; errmsg += def;
	mErrRet(); 
    }

    desc->setHidden( true );
    desc->setUserRef( nlamodel->name() );

    const int nrinputs = desc->nrInputs();

    for ( int idx=0; idx<nrinputs; idx++ )
    {
	const char* inpname = desc->inputSpec(idx).getDesc();
	DescID descid = ads->getID( inpname, true );
	if ( descid < 0 && IOObj::isKey(inpname) )
	{
	    descid = ads->getID( inpname, false );
	    if ( descid < 0 )
	    {
		// It could be 'storage', but it's not yet in the set ...
		PtrMan<IOObj> ioobj = IOM().get( MultiID(inpname) );
		if ( ioobj )
		{
		    Desc* stordesc = 
			PF().createDescCopy( StorageProvider::attribName() );
		    stordesc->setDescSet( ads );
		    ValParam* idpar = 
			stordesc->getValParam( StorageProvider::keyStr() );
		    idpar->setValue( inpname );
		    stordesc->setUserRef( ioobj->name() );
		    descid = ads->addDesc( stordesc );
		    if ( descid < 0 )
		    {
			errmsg = "NLA input '";
			errmsg += inpname;
			errmsg += "' cannot be found in the provided set.";
			mErrRet();
		    }
		}
	    }
	}

	desc->setInput( idx, ads->getDesc(descid) );
    }

    if ( attrspec.id() > desc->nrOutputs() )
    {
	errmsg = "Output "; errmsg += attrspec.id().asInt(); 
	errmsg += " not present.";
	mErrRet();
    }
    
    desc->selectOutput( attrspec.id().asInt() );

    outpid = ads->addDesc( desc );
    if ( outpid == DescID::undef() )
    {
	errmsg = ads->errMsg();
	mErrRet();
    }

    return ads;
}


ExecutorGroup* EngineMan::createExecutorGroup() const
{
    BufferString nm = createExecutorName();
    ExecutorGroup* procgroup = new ExecutorGroup( nm );
    procgroup->setNrDoneText( "Nr done" );
    for ( int idx=0; idx<procset.size(); idx++ )
	procgroup->add( procset[idx] );

    return procgroup;
}


BufferString EngineMan::createExecutorName() const
{
    BufferString usernm( curUserDesc() );
    if ( usernm == "" || !inpattrset ) return "";
    if ( IOObj::isKey(usernm) )
    {
	IOObj* ioobj = IOM().get( MultiID(usernm.buf()) );
	if ( ioobj )
	{
	    usernm = ioobj->name();
	    delete ioobj;
	}
    }

    BufferString nm;
    if ( attrspec.isNLA() )
    {
	nm = "Applying ";
	nm += nlamodel->nlaType(true);
	nm += ": calculating";
    }
    else
    {
	const Desc* ad = inpattrset->getDesc( attrspec.id() );
	if ( ad->isStored() )
	    nm = "Reading from";
    }

    nm += " \"";
    nm += usernm;
    nm += "\"";

    return nm;
}


#undef mErrRet
#define mErrRet(s) { errmsg = s; return 0; }

#define mStepEps 1e-3


ExecutorGroup* EngineMan::screenOutput2DCreator( BufferString& errmsg )
{
    if ( !getProcessors(procset,errmsg,!outattribs.size()) ) 
    { deepErase( procset ); return 0; }
    
    for ( int idx=0; idx<procset.size(); idx++ )
    {
	LineKey lkey(linekey.buf(),procset[idx]->getAttribName());
	CubeOutput* attrout = new CubeOutput( cs_, lkey );
	attrout->set2D();
	attrout->setGeometry( cs_ );
	procset[idx]->addOutput( attrout ); 
    }

    return createExecutorGroup();
}


ExecutorGroup* EngineMan::sliceSetOutputCreator( BufferString& errmsg,
				      const SliceSet* prev )
{
    if ( cs_.isEmpty() )
	prev = 0;
#define mRg(dir) (prev->sampling.dir##rg)
    else if ( prev )
    {
	cache = const_cast<SliceSet*>(prev);
	cache->ref();
	if ( !mRg(z).isCompatible( cs_.zrg, mStepEps )
	  || mRg(h).step != cs_.hrg.step
	  || (mRg(h).start.inl - cs_.hrg.start.inl) % cs_.hrg.step.inl
	  || (mRg(h).start.crl - cs_.hrg.start.crl) % cs_.hrg.step.crl 
	  || mRg(h).start.inl > cs_.hrg.stop.inl
	  || mRg(h).stop.inl < cs_.hrg.start.inl
	  || mRg(h).start.crl > cs_.hrg.stop.crl
	  || mRg(h).stop.crl < cs_.hrg.start.crl
	  || mRg(z).start > cs_.zrg.stop + mStepEps*cs_.zrg.step
	  || mRg(z).stop < cs_.zrg.start - mStepEps*cs_.zrg.step )
	    // No overlap, gotta crunch all the numbers ...
	    prev = 0;
    }

#define mAddAttrOut(todocs) \
{ \
    SliceSetOutput* attrout = new SliceSetOutput(todocs); \
    attrout->setGeometry( todocs ); \
    attrout->setUndefValue( udfval ); \
    for ( int idx=0; idx<procset.size(); idx++ )\
	procset[idx]->addOutput( attrout ); \
}

    if ( !getProcessors(procset,errmsg,!outattribs.size()) ) 
    { deepErase( procset ); return 0; }

    if ( !prev )
	mAddAttrOut( cs_ )
    else
    {
	CubeSampling todocs( cs_ );
	if ( mRg(h).start.inl > cs_.hrg.start.inl )
	{
	    todocs.hrg.stop.inl = mRg(h).start.inl - cs_.hrg.step.inl;
	    mAddAttrOut( todocs )
	}

	if ( mRg(h).stop.inl < cs_.hrg.stop.inl )
	{
	    todocs = cs_;
	    todocs.hrg.start.inl = mRg(h).stop.inl + cs_.hrg.step.inl;
	    mAddAttrOut( todocs )
	}

	const int startinl = mMAX(cs_.hrg.start.inl, mRg(h).start.inl );
	const int stopinl = mMIN( cs_.hrg.stop.inl, mRg(h).stop.inl );

	if ( mRg(h).start.crl > cs_.hrg.start.crl )
	{
	    todocs = cs_;
	    todocs.hrg.start.inl = startinl; todocs.hrg.stop.inl = stopinl;
	    todocs.hrg.stop.crl = mRg(h).start.crl - cs_.hrg.step.crl;
	    mAddAttrOut( todocs )
	}
	
	if ( mRg(h).stop.crl < cs_.hrg.stop.crl )
	{
	    todocs = cs_;
	    todocs.hrg.start.inl = startinl; todocs.hrg.stop.inl = stopinl;
	    todocs.hrg.start.crl = mRg(h).stop.crl + cs_.hrg.step.crl;
	    mAddAttrOut( todocs )
	}

	todocs = cs_;
	todocs.hrg.start.inl = startinl; todocs.hrg.stop.inl = stopinl;
	todocs.hrg.start.crl = mMAX(cs_.hrg.start.crl, mRg(h).start.crl );
	todocs.hrg.stop.crl = mMIN(cs_.hrg.stop.crl, mRg(h).stop.crl );

	if ( mRg(z).start > cs_.zrg.start + mStepEps*cs_.zrg.step )
	{
	    todocs.zrg.stop = mRg(z).start - cs_.zrg.step;
	    mAddAttrOut( todocs )
	}
	    
	if ( mRg(z).stop < cs_.zrg.stop - mStepEps*cs_.zrg.step )
	{
	    todocs.zrg = cs_.zrg;
	    todocs.zrg.start = mRg(z).stop + cs_.zrg.step;
	    mAddAttrOut( todocs )
	}
    }

    return createExecutorGroup();
}


class AEMFeatureExtracter : public ExecutorGroup
{
public:
AEMFeatureExtracter( EngineMan& aem, const BufferStringSet& inputs,
		     const ObjectSet<BinIDValueSet>& bivsets )
	: ExecutorGroup("Attribute Extraction at locations")
{
    const int nrinps = inputs.size();
    const DescSet* attrset = aem.procattrset ? aem.procattrset : aem.inpattrset;
    for ( int idx=0; idx<attrset->nrDescs(); idx++ )
    {
	const DescID descid = attrset->getID( idx );
	const Desc* ad = attrset->getDesc( descid );
	if ( !ad ) continue;

	bool dosel = false;
	for ( int iinp=0; iinp<nrinps; iinp++ )
	{
	    if ( ad->isIdentifiedBy(inputs.get(iinp)) )
		{ dosel = true; break; }
	}

	if ( dosel ) aem.outattribs += descid;
    }

    ObjectSet<BinIDValueSet>& bvs = 
	const_cast<ObjectSet<BinIDValueSet>&>(bivsets);
    exec = aem.locationOutputCreator( errmsg, bvs );
}

int totalNr() const		{ return exec ? exec->totalNr() : -1; }
int nrDone() const		{ return exec ? exec->nrDone() : 0; }
const char* nrDoneText() const	{ return exec ? exec->nrDoneText() : ""; }

const char* message() const
{
    return *(const char*)errmsg ? errmsg.buf() 
	: (exec ? exec->message() : "Cannot create output");
}

int haveError( const char* msg )
{
    if ( msg ) errmsg = msg;
    return -1;
}

int nextStep()
{
    if ( !exec ) return haveError( 0 );

    int rv = exec->doStep();
    if ( rv >= 0 ) return rv;
    return haveError( exec->message() );
}

    BufferString		errmsg;
    ExecutorGroup*		exec;
};


ExecutorGroup* EngineMan::featureOutputCreator(
			const BufferStringSet& inputs,
			const ObjectSet<BinIDValueSet>& bivsets )
{
    return new AEMFeatureExtracter( *this, inputs, bivsets );
}


ExecutorGroup* EngineMan::locationOutputCreator( BufferString& errmsg,
				ObjectSet<BinIDValueSet>& bidzvset )
{
    if ( bidzvset.size() == 0 ) mErrRet("No locations to extract data on")

    if ( !getProcessors(procset,errmsg,!outattribs.size())  )
    { deepErase( procset ); return 0; }

    ObjectSet<LocationOutput> outputs;
    for ( int idx=0; idx<bidzvset.size(); idx++ )
    {
	BinIDValueSet& bidzvs = *bidzvset[idx];
	LocationOutput* attrout = new LocationOutput( bidzvs );
	outputs += attrout;
    }
    
    if ( !outputs.size() )
        return 0;

    for ( int idx=0; idx<outputs.size(); idx++ )
    {
	for ( int idy=0; idy<procset.size(); idy++ )
	    procset[idy]->addOutput( outputs[idx] );
    }

    int index = 0;
    for ( int idy=0; idy<procset.size(); idy++ )
	procset[idy]->setOutputIndex( index );

    return createExecutorGroup();
}

#undef mErrRet
#define mErrRet(s) { errmsg = s; return false; }

bool EngineMan::getProcessors( ObjectSet<Processor>& pset, 
			       BufferString& errmsg, bool needid )
{
    errmsg = "";
    if ( procattrset )
	{ delete procattrset; procattrset = 0; }

    if ( !inpattrset ) mErrRet("No Attribute Set yet")
    if ( needid && attrspec.id() < 0 )
	mErrRet(attrspec.isNLA()?"No NLA available":"No Attribute selected");

    curattrid = attrspec.id();

    if ( !attrspec.isNLA() )
    {
	if ( needid )
	    procattrset = inpattrset->optimizeClone( attrspec.id() );
	else
	    procattrset = inpattrset->clone();
    }
    else
    {
	inpattrset->fillPar( const_cast<NLAModel*>(nlamodel)->pars() );
	procattrset = createNLAADS( curattrid, errmsg );
	if ( *(const char*)errmsg )
	    mErrRet(errmsg)
    }

    if ( needid && outattribs.indexOf(curattrid)<0 )
	outattribs.insert(0,curattrid);

    createProcSet( pset, *procattrset, lineKey().buf(), outattribs );
    if ( !pset.size() )
    {
	errmsg = "No processor created";
	return false;
    }

    return true;
}


ExecutorGroup* EngineMan::trcSelOutputCreator( BufferString& errmsg,
					       const BinIDValueSet& bidvalset,
					       SeisTrcBuf& output )
{
    if ( !getProcessors(procset,errmsg,!outattribs.size()) )
    { deepErase( procset ); return 0; }

    TrcSelectionOutput* attrout	= new TrcSelectionOutput( bidvalset );
    attrout->setOutput( &output );

    for ( int idx=0; idx<procset.size(); idx++ )
	procset[idx]->addOutput( attrout );

    return createExecutorGroup();
};

} // namespace Attrib
