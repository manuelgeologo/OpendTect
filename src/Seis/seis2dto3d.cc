/*+
________________________________________________________________________

(C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
Author:        Bruno
Date:          Feb 2011
________________________________________________________________________

-*/


static const char* rcsID mUsedVar = "$Id$";

#include "seis2dto3d.h"

#include "arrayndalgo.h"
#include "bufstring.h"
#include "dataclipper.h"
#include "fftfilter.h"
#include "ioman.h"
#include "ioobj.h"
#include "keystrs.h"
#include "ptrman.h"
#include "scaler.h"
#include "seis2dline.h"
#include "seisbuf.h"
#include "seisioobjinfo.h"
#include "seisjobexecprov.h"
#include "seisread.h"
#include "seisselectionimpl.h"
#include "seistrc.h"
#include "seistrcprop.h"
#include "seiswrite.h"
#include "survinfo.h"



const char* Seis2DTo3D::sKeyInput()	{ return "Input ID"; }
const char* Seis2DTo3D::sKeyIsNearest()	{ return "Take Nearest"; }
const char* Seis2DTo3D::sKeyStepout()	{ return "Inl-Crl Stepout"; }
const char* Seis2DTo3D::sKeyReUse()	{ return "Re-use existing"; }
const char* Seis2DTo3D::sKeyMaxVel()	{ return "Maximum Velocity"; }



Seis2DTo3D::Seis2DTo3D()
    : Executor("Generating 3D cube from LineSet")
    , ls_(0)
    , outioobj_(0)
    , cs_(true)
    , read_(false)
    , seisbuf_(*new SeisTrcBuf(false))
    , nrdone_(0)
    , sc_(0)
    , wrr_(0)
    , tmpseisbuf_(true)
    , maxvel_(mUdf(float))
    , inlstep_(0)
    , crlstep_(0)
{}


Seis2DTo3D::~Seis2DTo3D()
{
    seisbuf_.erase();
    delete wrr_; wrr_ = 0;
    if ( !nearesttrace_ && sc_ )
	delete sc_; sc_ = 0;
}


bool Seis2DTo3D::init( const IOPar& pars )
{ return usePar(pars); }


#define mErrRet(msg) { errmsg_ = msg; return false; }
bool Seis2DTo3D::usePar( const IOPar& pars )
{
    if ( !setIO(pars) )
	return false;

    PtrMan<IOPar> parampars = pars.subselect( sKey::Pars() );
    if ( !parampars )
	mErrRet("No processing parameters found")

    parampars->getYN( sKeyIsNearest(), nearesttrace_ );
    if ( !nearesttrace_ )
    {
	Interval<int> step;
	parampars->get( sKeyStepout(), step );
	inlstep_ = step.start;
	crlstep_ = step.stop;
	parampars->getYN( sKeyReUse(), reusetrcs_ );
	parampars->get( sKeyMaxVel(), maxvel_ );
	cs_.hrg.step = BinID( inlstep_, crlstep_ );
    }

    return true;
}


bool Seis2DTo3D::setIO( const IOPar& pars )
{
    MultiID key;
    pars.get( sKeyInput(), key );
    PtrMan<IOObj> input = IOM().get( key );
    if ( !input )
	mErrRet("Lineset not found")

    ls_ = new Seis2DLineSet( *input );
    pars.get( sKey::Attribute(), attrnm_ );
    if ( attrnm_.isEmpty() )
	mErrRet("DataSet name not found")

    pars.get( SeisJobExecProv::sKeySeisOutIDKey(), key );
    outioobj_ = IOM().get( key );
    if ( !outioobj_ )
	mErrRet("Output cube entry not found")

    PtrMan<IOPar> subsel = pars.subselect( sKey::Output() );
    if ( !subsel ) return false;

    PtrMan<IOPar> sampling = subsel->subselect( sKey::Subsel() );
    if ( !sampling )
	mErrRet("No volume processing area found")

    cs_.usePar( *sampling );

    return true;
}


bool Seis2DTo3D::checkParameters()
{
    if ( nearesttrace_ ) return true;

    if ( inlstep_ < 1 && crlstep_ < 1 )
    {
	BufferString msg( "Internal: " );
	msg.add( inlstep_<1 ? "Inline" : "Crossline" );
	msg.add( " step is not set" );
	mErrRet(msg)
    }

    if ( mIsUdf(maxvel_) )
	mErrRet( "Internal: Maximum velocity is not set" )

    return true;
}


bool Seis2DTo3D::read()
{
    if ( ls_->nrLines() < 1 )
	mErrRet( "Empty LineSet" )

    BufferStringSet lnms;
    ls_->getLineNamesWithAttrib( lnms, attrnm_.buf() );
    if ( lnms.isEmpty() )
	mErrRet( "No lines for this attribute" )

    Interval<int> inlrg( cs_.hrg.inlRange().start - inlstep_,
			 cs_.hrg.inlRange().stop + inlstep_ );
    Interval<int> crlrg( cs_.hrg.crlRange().start - crlstep_,
			 cs_.hrg.crlRange().stop + crlstep_ );
    SeisTrcBuf tmpbuf(false);
    for ( int iline=0; iline<lnms.size(); iline++)
    {
	const LineKey lk( lnms.get( iline ), attrnm_.buf() );
	const int lsidx = ls_->indexOf( lk );
	if ( lsidx < 0 )
	    continue;

	Executor* lf = ls_->lineFetcher( lsidx, tmpbuf );
	lf->execute();
	for ( int idx=tmpbuf.size()-1; idx>=0; idx-- )
	{
	    const SeisTrc& intrc = *tmpbuf.get( idx );
	    const BinID bid = intrc.info().binid;
	    if ( !inlrg.includes(bid.inl,false) ||
		 !crlrg.includes(bid.crl,false) )
		continue;

	    SeisTrc* trc = new SeisTrc( intrc );
	    const int ns = cs_.zrg.nrSteps() + 1;
	    trc->reSize( ns, false );
	    trc->info().sampling.start = cs_.zrg.start;
	    trc->info().sampling.step = cs_.zrg.step;
	    for ( int isamp=0; isamp<ns; isamp++ )
	    {
		const float z = cs_.zrg.atIndex( isamp );
		for ( int icomp=0; icomp<intrc.nrComponents(); icomp++ )
		    trc->set( isamp, intrc.getValue(z,icomp), icomp );
	    }
	    seisbuf_.add( trc );
	}

	tmpbuf.erase();
    }

    if ( seisbuf_.isEmpty() )
	return true;

    hsit_.setSampling( cs_.hrg );

    delete ls_;
    if ( !nearesttrace_ )
	sc_ = new SeisScaler( seisbuf_ );

    read_ = true;
    return true;
}


int Seis2DTo3D::nextStep()
{
    if ( ( !read_ && !read() ) || !checkParameters() )
	return ErrorOccurred();

    if ( !hsit_.next(curbid_) )
	{ writeTmpTrcs(); return Finished(); }

    if ( !SI().includes( curbid_, 0, true ) )
	return MoreToDo();

    if ( nrdone_ == 0 )
	prevbid_ = curbid_;

    if ( curbid_.inl != prevbid_.inl )
    {
	if ( !writeTmpTrcs() )
	    { errmsg_ = "Can not write trace"; return ErrorOccurred(); }
	prevbid_ = curbid_;
    }

    if ( nearesttrace_ )
    {
	doWorkNearest();
    }
    else
    {
	if ( !doWorkFFT() )
	    return ErrorOccurred();
    }

    nrdone_++;
    return MoreToDo();
}


void Seis2DTo3D::doWorkNearest()
{
    od_int64 mindist = mUdf(od_int64);
    const SeisTrc* nearesttrc = 0;
    for( int idx=0; idx<seisbuf_.size(); idx++ )
    {
	const SeisTrc* trc = seisbuf_.get( idx );
	const BinID bid = trc->info().binid;

	if ( bid == curbid_ )
	{
	    nearesttrc = trc;
	    break;
	}

	int xx0 = bid.inl-curbid_.inl;     xx0 *= xx0;
	int yy0 = bid.crl-curbid_.crl;     yy0 *= yy0;

	if ( (  xx0 + yy0  ) < mindist || mIsUdf(mindist) )
	{
	    nearesttrc = trc;
	    mindist = xx0 + yy0;
	}
    }

    if ( !nearesttrc )
	return;

    SeisTrc* newtrc = new SeisTrc( *nearesttrc );
    newtrc->info().binid = curbid_;
    tmpseisbuf_.add( newtrc );
}


bool Seis2DTo3D::doWorkFFT()
{
    const int inl = curbid_.inl; const int crl = curbid_.crl;
    Interval<int> inlrg( inl-inlstep_, inl+inlstep_ );
    Interval<int> crlrg( crl-crlstep_, crl+crlstep_ );
    inlrg.limitTo( SI().inlRange(true) );
    crlrg.limitTo( SI().crlRange(true) );
    HorSampling hrg; hrg.set( inlrg, crlrg );
    hrg.step = BinID( SI().inlRange(true).step, SI().crlRange(true).step );
    HorSamplingIterator localhsit( hrg );
    BinID binid;
    ObjectSet<const SeisTrc> trcs;
    if ( !outioobj_ )
	mErrRet("Internal: No output is set")

    SeisTrcReader rdr( outioobj_ );
    SeisTrcBuf outtrcbuf(false);
    SeisBufReader sbrdr( rdr, outtrcbuf );
    sbrdr.execute();
    while ( localhsit.next(binid) )
    {
	const int idtrc = seisbuf_.find( binid  );
	if ( idtrc >= 0 )
	    trcs += seisbuf_.get( idtrc );
	else if ( reusetrcs_ && !tmpseisbuf_.isEmpty() )
	{
	    const int idinterptrc = tmpseisbuf_.find( binid  );
	    if ( idinterptrc >= 0 )
		trcs += tmpseisbuf_.get( idinterptrc );
	}
	else if ( reusetrcs_ && !outtrcbuf.isEmpty() )
	{
	    const int outidtrc = outtrcbuf.find( binid  );
	    if ( outidtrc >= 0 )
		trcs += outtrcbuf.get( outidtrc );
	}
    }
    interpol_.setInput( trcs );
    interpol_.setParams( hrg, maxvel_);
    if ( !trcs.isEmpty() && !interpol_.execute() )
    { errmsg_ = interpol_.errMsg(); return false; }

    Interval<int> wininlrg( inl-inlstep_/2, inl+inlstep_/2);
    Interval<int> wincrlrg( crl-crlstep_/2, crl+crlstep_/2);
    wininlrg.limitTo( SI().inlRange(true) );
    wincrlrg.limitTo( SI().crlRange(true) );
    HorSampling winhrg; winhrg.set( cs_.hrg.inlRange(), cs_.hrg.crlRange() );
    winhrg.step = BinID(SI().inlRange(true).step,SI().crlRange(true).step);
    ObjectSet<SeisTrc> outtrcs;
    interpol_.getOutTrcs( outtrcs, winhrg );

    if ( outtrcs.isEmpty() )
    {
	BinID bid;
	HorSamplingIterator hsit( winhrg );
	while ( hsit.next(bid) && seisbuf_.get(0) )
	{
	    SeisTrc* trc = new SeisTrc( seisbuf_.get( 0 )->size() );
	    trc->info().sampling = seisbuf_.get( 0 )->info().sampling;
	    trc->info().binid = bid;
	}
    }

    for ( int idx=0; idx<outtrcs.size(); idx ++ )
    {
	sc_->scaleTrace( *outtrcs[idx] );
	tmpseisbuf_.add( outtrcs[idx] );
    }

    return true;
}


bool Seis2DTo3D::writeTmpTrcs()
{
    if ( tmpseisbuf_.isEmpty() )
	return true;

    if ( !wrr_ )
	wrr_ = new SeisTrcWriter( outioobj_ );

    tmpseisbuf_.sort( true, SeisTrcInfo::BinIDInl );
    int curinl = tmpseisbuf_.get( 0 )->info().binid.inl;
    int previnl = curinl;
    SeisTrcBuf tmpbuf(true);
    bool isbufempty = false;
    while ( !tmpseisbuf_.isEmpty() )
    {
	SeisTrc* trc = tmpseisbuf_.remove(0);
	curinl = trc->info().binid.inl;
	isbufempty = tmpseisbuf_.isEmpty();
	if ( previnl != curinl || isbufempty )
	{
	    tmpbuf.sort( true, SeisTrcInfo::BinIDCrl );
	    int prevcrl = -1;
	    while( !tmpbuf.isEmpty() )
	    {
		const SeisTrc* crltrc = tmpbuf.remove(0);
		const int curcrl = crltrc->info().binid.crl;
		if ( curcrl != prevcrl )
		{
		    if ( !wrr_->put( *crltrc ) )
			return false;
		}
		prevcrl = curcrl;
		delete crltrc;
	    }
	    previnl = curinl;
	}
	tmpbuf.add( trc );
    }
    return true;
}


od_int64 Seis2DTo3D::totalNr() const
{
    return cs_.hrg.totalNr();
}



SeisInterpol::SeisInterpol()
    : Executor("Interpolating")
    , hs_(false)
    , fft_(0)
    , nrdone_(0)
    , max_(0)
    , trcarr_(0)
{}


SeisInterpol::~SeisInterpol()
{
    clear();
    delete trcarr_;
}


void SeisInterpol::clear()
{
    errmsg_.setEmpty();
    nriter_ = 10;
    totnr_ = -1;
    nrdone_ = 0;
    szx_ = szy_ = szz_ = 0;
    max_ = 0;
    maxvel_ = 0;
    posidxs_.erase();
}


void SeisInterpol::setInput( const ObjectSet<const SeisTrc>& trcs )
{
    clear();
    inptrcs_ = &trcs;
}


void SeisInterpol::setParams( const HorSampling& hs, float maxvel )
{
    hs_ = hs;
    maxvel_ = maxvel;
}


void SeisInterpol::doPrepare()
{
    delete fft_;
    fft_ = Fourier::CC::createDefault();

    const StepInterval<int>& inlrg = hs_.inlRange();
    const StepInterval<int>& crlrg = hs_.crlRange();

    const int hsszx = inlrg.nrSteps() + 1;
    const int hsszy = crlrg.nrSteps() + 1;

    szx_ = fft_->getFastSize( hsszx );
    szy_ = fft_->getFastSize( hsszy );
    szz_ = fft_->getFastSize( (*inptrcs_)[0]->size() );

    const int diffszx = szx_ - hsszx;
    const int diffszy = szy_ - hsszy;

    hs_.setInlRange(Interval<int>(inlrg.start-diffszx/2,inlrg.stop+diffszx/2));
    hs_.setCrlRange(Interval<int>(crlrg.start-diffszy/2,crlrg.stop+diffszy/2));

    setUpData();
}


#define mDefThreshold ( (float)(nriter_-nrdone_-1)/ (float)nriter_ )
#define mDoTransform(tf,isstraight,arr) \
{\
    tf->setInputInfo( arr->info() );\
    tf->setDir(isstraight);\
    tf->setNormalization(!isstraight);\
    tf->setInput(arr->getData());\
    tf->setOutput(arr->getData());\
    tf->run(true);\
}

int SeisInterpol::nextStep()
{
    if ( nrdone_ == 0 )
	doPrepare();

    for ( int idtrc=0; idtrc<posidxs_.size(); idtrc++ )
    {
	TrcPosTrl& trpos = posidxs_[idtrc];
	if ( trpos.trcpos_ >= inptrcs_->size() )
	    continue;

	for ( int idz=0; idz<szz_; idz++ )
	{
	    float val = 0;
	    if ( idz < (*inptrcs_)[0]->size() )
		val = (*inptrcs_)[trpos.trcpos_]->get(idz,0);
	    trcarr_->set( trpos.idx_, trpos.idy_, idz, val );
	}
    }

    if ( nrdone_ == nriter_ )
	{ return Executor::Finished(); }

    mDoTransform( fft_, true, trcarr_ );
    const float df = Fourier::CC::getDf( SI().zStep(), szz_ );
    const float mindist = mMIN(SI().inlDistance(),SI().crlDistance() );
    const float fmax = (float) (maxvel_ / ( 2.f*mindist*sin( M_PIf/6.f ) ));
    const int poscutfreq = (int)(fmax/df);

#define mDoLoopWork( docomputemax )\
    for ( int idx=0; idx<szx_; idx++ )\
    {\
	for ( int idy=0; idy<szy_; idy++ )\
	{\
	    for ( int idz=0; idz<szz_; idz++ )\
	    {\
		float real = trcarr_->get(idx,idy,idz).real();\
		float imag = trcarr_->get(idx,idy,idz).imag();\
		float xfac; float yfac; float zfac;\
		xfac = yfac = zfac = 0;\
		if ( idz < poscutfreq || idz > szz_-poscutfreq )\
		    zfac = 1;\
		float dipangle; float revdipangle;\
		dipangle = revdipangle = 0;\
		if ( idx < szx_/2 )\
		{\
		    dipangle = atan( idy/(float)idx );\
		    revdipangle = atan( (szy_-idy-1)/(float)(idx) );\
		}\
		else\
		{\
		    dipangle = atan( idy/(float)(szx_-idx-1) );\
		    revdipangle = atan( (szy_-idy-1)/(float)(szx_-idx-1) );\
		}\
		if ( dipangle > M_PI_4f && revdipangle > M_PI_4f )\
		    { xfac = yfac = 1; }\
		real *= xfac*yfac*zfac; imag *= xfac*yfac*zfac;\
		float mod = real*real + imag*imag;\
		if ( docomputemax )\
		{\
		    mod = real*real + imag*imag;\
		    if ( mod > max_ )\
			max_ = mod;\
		}\
		else\
		{\
		    if ( mod < max_*mDefThreshold )\
			{ real = imag = 0; }\
		    trcarr_->set(idx,idy,idz,float_complex(real,imag));\
		}\
	    }\
	}\
    }

    if ( nrdone_ == 0 )
	{ mDoLoopWork( true ) }

    mDoLoopWork( false )
    mDoTransform( fft_, false, trcarr_ );

    nrdone_++;
    return Executor::MoreToDo();
}


const BinID SeisInterpol::convertToBID( int idx, int idy ) const
{
    return BinID( hs_.inlRange().atIndex(idx), hs_.crlRange().atIndex(idy) );
}


void SeisInterpol::convertToPos( const BinID& bid, int& idx, int& idy ) const
{
    idx = hs_.inlRange().getIndex( bid.inl );
    idy = hs_.crlRange().getIndex( bid.crl );
}


int SeisInterpol::getTrcInSet( const BinID& bin ) const
{
    for ( int idx=0; idx<inptrcs_->size(); idx++ )
    {
	const SeisTrc* trc = (*inptrcs_)[idx];
	if ( trc->info().binid == bin )
	   return idx;
    }
    return -1;
}


void SeisInterpol::getOutTrcs( ObjectSet<SeisTrc>& trcs,
				const HorSampling& hs) const
{
    if ( inptrcs_->isEmpty() )
	return;

    HorSamplingIterator hsit( hs_ );
    BinID bid;
    while ( hsit.next( bid ) )
    {
	if ( !hs.includes(bid) )
	    continue;

	int idx = -1; int idy = -1;
	convertToPos( bid, idx, idy );
	if ( idx < 0 || idy < 0 || szx_ <= idx || szy_ <= idy) continue;

	SeisTrc* trc = new SeisTrc( szz_ );
	trc->info().sampling = (*inptrcs_)[0]->info().sampling;
	trc->info().binid = bid;
	for ( int idz=0; idz<szz_; idz++ )
	{
	    float val = trcarr_->get( idx, idy, idz ).real();
	    if ( mIsUdf(val) ) val = 0;
	    trc->set( idz, val, 0 );
	}
	trcs += trc;
    }
}


void SeisInterpol::setUpData()
{
    if ( !trcarr_)
	trcarr_ = new Array3DImpl<float_complex>( szx_, szy_, szz_ );
    else
	trcarr_->setSize( szx_, szy_, szz_ );
    trcarr_->setAll( 0 );

    for ( int idx=0; idx<szx_; idx++ )
    {
	for ( int idy=0; idy<szy_; idy++ )
	{
	    const BinID bid = convertToBID( idx, idy );
	    const int trcidx = getTrcInSet( bid );
	    if ( trcidx >= 0 )
		posidxs_ += TrcPosTrl( idx, idy, trcidx );
	}
    }
}


#define mGetTrcRMSVal(tr,max,min)\
    Interval<float> rg;\
    DataClipper cl; cl.setApproxNrValues( tr.size() );\
    TypeSet<float> vals; \
    for ( int idx=0; idx<tr.size(); idx ++ )\
        vals += tr.get( idx, 0 );\
    cl.putData( vals.arr(), tr.size() );\
    cl.calculateRange( 0.1, rg );\
    max = rg.stop; min = rg.start;\

SeisScaler::SeisScaler( const SeisTrcBuf& trcs )
    : avgmaxval_(0)
    , avgminval_(0)
{
    for ( int idtrc=0; idtrc<trcs.size(); idtrc++ )
    {
	float maxval, minval;
	const SeisTrc& curtrc = *trcs.get( idtrc );
	mGetTrcRMSVal( curtrc, maxval, minval )
	avgmaxval_ += maxval / trcs.size();
	avgminval_ += minval / trcs.size();
    }
}


void SeisScaler::scaleTrace( SeisTrc& trc )
{
    float trcmaxval, trcminval;
    mGetTrcRMSVal( trc, trcmaxval, trcminval )
    LinScaler sc( trcminval, avgminval_, trcmaxval, avgmaxval_ );
    for ( int idz=0; idz<trc.size(); idz++ )
    {
	float val = (float) trc.get( idz, 0 );
	val = (float) sc.scale( val );
	trc.set( idz, val, 0 );
    }
}

