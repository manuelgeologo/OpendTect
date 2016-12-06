/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:	Mahant Mothey
 Date:		September 2016
________________________________________________________________________

-*/

#include "seisvolprovider.h"
#include "seistrctr.h"
#include "seispreload.h"
#include "seisdatapack.h"
#include "dbman.h"
#include "iostrm.h"
#include "uistrings.h"
#include "seisioobjinfo.h"
#include "seispacketinfo.h"
#include "seisselection.h"
#include "seisdatapack.h"
#include "posinfo.h"
#include "survinfo.h"
#include "file.h"


od_int64 Seis::Provider3D::getTotalNrInInput() const
{
    PosInfo::CubeData cd;
    getGeometryInfo( cd );
    if ( !cd.isEmpty() )
	return cd.totalSize();

    const od_int64 nrinls = SI().inlRange(false).nrSteps() + 1;
    const od_int64 nrcrls = SI().inlRange(false).nrSteps() + 1;
    return nrinls * nrcrls;
}


namespace Seis
{

/*\brief Gets required traces from either DataPack or Translator

  Both DataPack and Translator have a natural 'next' postion to go to. What we
  need to tackle is when the seclection requires reading from both DataPack
  and Translator. The strategy is to set up a required area, the boundary
  of the selection. The Translator has its own 'current' position, but we
  have our own 'next BinID' anyway.

  There is no glue-ing going on in the Z direction. If the DataPack cannot
  satisfy the Z range requirements, then it will not be used.

  */

class VolFetcher
{ mODTextTranslationClass(Seis::VolFetcher);
public:

VolFetcher( VolProvider& p )
    : prov_(p)
    , trl_(0)
    , ioobj_(0)
{
}

~VolFetcher()
{
    delete trl_;
    delete ioobj_;
}

    void		reset();
    void		getReqCS();
    void		findDataPack();
    void		openCube();
    bool		isSelectedBinID(const BinID&) const;
    bool		moveNextBinID();
    bool		advanceTrlToNextSelected(SeisTrcInfo&);
    Conn*		getConn();
    void		getNextTranslator();
    void		getTranslator(Conn*);
    bool		isMultiConn() const;
    bool		translatorSelected() const;

    void		get(const TrcKey&,SeisTrc&);
    void		getNext(SeisTrc&);

    VolProvider&	prov_;
    IOObj*		ioobj_;

    RefMan<RegularSeisDataPack> dp_;
    TrcKeyZSampling	reqcs_;
    BinID		nextbid_;

    SeisTrcTranslator*	trl_;

    uiRetVal		uirv_;

};

} // namespace Seis


void Seis::VolFetcher::reset()
{
    uirv_.setEmpty();
    delete trl_; trl_ = 0;
    delete ioobj_; ioobj_ = 0;
    dp_ = 0;
    nextbid_.inl() = mUdf(int);

    findDataPack();
    getReqCS();
    if ( !dp_ || !dp_->sampling().includes(reqcs_) )
	openCube();

    nextbid_ = reqcs_.hsamp_.start_ - reqcs_.hsamp_.step_;
    if ( !moveNextBinID() )
	uirv_.set( tr("No traces available for current selection") );
}


bool Seis::VolFetcher::isSelectedBinID( const BinID& bid ) const
{
    return !prov_.seldata_ || prov_.seldata_->isOK(bid);
}


bool Seis::VolFetcher::moveNextBinID()
{
    while ( true )
    {
	if ( !reqcs_.hsamp_.toNext(nextbid_) )
	    return false;
	if ( isSelectedBinID(nextbid_) )
	    return true;
    }
}



void Seis::VolFetcher::getReqCS()
{
    // set the default to everything; also sets proper steps
    SeisIOObjInfo objinf( prov_.dbky_ );
    if ( !objinf.getRanges(reqcs_) )
    {
	if ( dp_ )
	    reqcs_ = dp_->sampling();
	else
	    reqcs_ = TrcKeyZSampling( true );
    }

    if ( prov_.seldata_ && !prov_.seldata_->isAll() )
    {
	const Seis::SelData& seldata = *prov_.seldata_;
	const Interval<float> reqzrg( seldata.zRange() );
	const Interval<int> reqinlrg( seldata.inlRange() );
	const Interval<int> reqcrlrg( seldata.crlRange() );
	reqcs_.zsamp_.start = reqzrg.start;
	reqcs_.zsamp_.stop = reqzrg.stop;
	reqcs_.hsamp_.start_.inl() = reqinlrg.start;
	reqcs_.hsamp_.stop_.inl() = reqinlrg.stop;
	reqcs_.hsamp_.start_.crl() = reqcrlrg.start;
	reqcs_.hsamp_.stop_.crl() = reqcrlrg.stop;
    }

    if ( dp_ && !dp_->sampling().zsamp_.includes(reqcs_.zsamp_) )
	dp_ = 0; // too bad but the required Z range is not loaded
}


void Seis::VolFetcher::findDataPack()
{
    RefMan<DataPack> dp = Seis::PLDM().get( prov_.dbky_ );
    if ( !dp )
	return;
    mDynamicCastGet(RegularSeisDataPack*,rdp,dp.ptr());
    if ( !rdp || rdp->isEmpty() )
	return;

    dp_ = rdp;
}


void Seis::VolFetcher::openCube()
{
    ioobj_ = DBM().get( prov_.dbky_ );
    if ( !ioobj_ )
    {
	uirv_ = uiStrings::phrCannotFindDBEntry( prov_.dbky_.toUiString() );
	return;
    }

    getNextTranslator();

    if ( !trl_ )
	{ uirv_ = tr( "No selected data found" ); return; }
}


void Seis::VolFetcher::getNextTranslator()
{
    while ( true )
    {
	Conn* conn = getConn();
	if ( !conn )
	    return;

	getTranslator( conn );
	if ( !trl_ )
	    return;

	if ( translatorSelected() )
	    break;
    }
}



Conn* Seis::VolFetcher::getConn()
{
    Conn* conn = ioobj_->getConn( Conn::Read );
    const BufferString fnm = ioobj_->fullUserExpr( Conn::Read );
    if ( !conn || (conn->isBad() && !File::isDirectory(fnm)) )
    {
	delete conn; conn = 0;
	mDynamicCastGet(IOStream*,iostrm,ioobj_)
	if ( iostrm && iostrm->isMultiConn() )
	{
	    while ( !conn || conn->isBad() )
	    {
		delete conn; conn = 0;
		if ( !iostrm->toNextConnIdx() )
		    break;

		conn = ioobj_->getConn( Conn::Read );
	    }
	}
    }

    if ( !conn )
	uirv_ = uiStrings::phrCannotOpen( ioobj_->uiName() );
    return conn;
}


void Seis::VolFetcher::getTranslator( Conn* conn )
{
    delete trl_;
    Translator* trl = ioobj_->createTranslator();
    mDynamicCast( SeisTrcTranslator*, trl_, trl );
    if ( !trl_ )
    {
	uirv_ = tr("Cannot create appropriate data reader."
	    "\nThis is an installation problem or a data corruption issue.");
	return;
    }

    trl_->setSelData( prov_.seldata_ );
    if ( !trl_->initRead(conn,prov_.readmode_) )
	{ uirv_ = trl_->errMsg(); delete trl_; trl_ = 0; return; }

    if ( prov_.selcomp_ >= 0 )
    {
	for ( int icd=0; icd<trl_->componentInfo().size(); icd++ )
	{
	    if ( icd != prov_.selcomp_ )
		trl_->componentInfo()[icd]->destidx = -1;
	}
    }

    if ( !trl_->commitSelections() )
	{ uirv_ = trl_->errMsg(); delete trl_; trl_ = 0; return; }
}


bool Seis::VolFetcher::isMultiConn() const
{
    mDynamicCastGet(IOStream*,iostrm,ioobj_)
    return iostrm && iostrm->isMultiConn();
}


bool Seis::VolFetcher::translatorSelected() const
{
    if ( !trl_ )
	return false;
    if ( !prov_.seldata_ || !isMultiConn() )
	return true;

    BinID bid( trl_->packetInfo().inlrg.start, trl_->packetInfo().crlrg.start );
    int selres = prov_.seldata_->selRes( bid );
    return selres / 256 == 0;
}


void Seis::VolFetcher::get( const TrcKey& trcky, SeisTrc& trc )
{
    bool moveok = false;
    nextbid_ = trcky.binID();
    if ( dp_ && dp_->sampling().hsamp_.includes( nextbid_ ) )
	moveok = true;
    else if ( trl_ && trl_->goTo(nextbid_) )
	moveok = true;

    if ( moveok )
	getNext( trc );
    else
	uirv_.set( tr("Position not present: %1/%2")
		    .arg( nextbid_.inl() ).arg( nextbid_.crl() ) );
}


bool Seis::VolFetcher::advanceTrlToNextSelected( SeisTrcInfo& ti )
{
    while ( true )
    {
	if ( !trl_->readInfo(ti) )
	{
	    if ( !isMultiConn() )
		uirv_.set( trl_->errMsg() );
	    else
	    {
		getNextTranslator();
		if ( trl_ )
		    return advanceTrlToNextSelected( ti );
	    }
	    if ( uirv_.isOK() )
		uirv_.set( uiStrings::sFinished() );
	    return false;
	}

	if ( isSelectedBinID(ti.binID()) )
	    { nextbid_ = ti.binID(); return true; }
	else
	    trl_->skip( 1 );
    }
    return false;
}


void Seis::VolFetcher::getNext( SeisTrc& trc )
{
    bool havefilled = false;

    if ( dp_ )
    {
	if ( dp_->sampling().hsamp_.includes(nextbid_) )
	{
	    dp_->fillTrace( TrcKey(nextbid_), trc );
	    havefilled = true;
	}
	else if ( !trl_ )
	{
	    if ( !moveNextBinID() )
		{ uirv_.set( uiStrings::sFinished() ); return; }
	    dp_->fillTrace( TrcKey(nextbid_), trc );
	    havefilled = true;
	}
    }

    if ( trl_ && !havefilled )
    {
	if ( !advanceTrlToNextSelected(trc.info()) )
	    return;

	if ( !trl_->read(trc) )
	    { uirv_.set( trl_->errMsg() ); return; }

	havefilled = true;
    }

    if ( havefilled )
    {
	trc.info().trckey_.setSurvID( TrcKey::std3DSurvID() );
	moveNextBinID();
    }
}



Seis::VolProvider::VolProvider()
    : fetcher_(*new VolFetcher(*this))
{
}


Seis::VolProvider::~VolProvider()
{
    delete &fetcher_;
}


BufferStringSet Seis::VolProvider::getComponentInfo() const
{
    BufferStringSet compnms;
    if ( fetcher_.dp_ )
    {
	for ( int icd=0; icd<fetcher_.dp_->nrComponents(); icd++ )
	    compnms.add( fetcher_.dp_->getComponentName(icd) );
    }
    else
    {
	if ( fetcher_.trl_ )
	{
	    for ( int icd=0; icd<fetcher_.trl_->componentInfo().size(); icd++ )
		compnms.add( fetcher_.trl_->componentInfo()[icd]->name() );
	}
	else
	{
	    SeisIOObjInfo objinf( dbky_ );
	    objinf.getComponentNames( compnms );
	}
    }
    return compnms;
}


ZSampling Seis::VolProvider::getZSampling() const
{
    ZSampling ret;
    if ( fetcher_.dp_ )
	ret = fetcher_.dp_->getZRange();
    else
    {
	if ( fetcher_.trl_ )
	{
	    ret.start = fetcher_.trl_->inpSD().start;
	    ret.step = fetcher_.trl_->inpSD().step;
	    ret.stop = ret.start + (fetcher_.trl_->inpNrSamples()-1) * ret.step;
	}
	else
	{
	    TrcKeyZSampling cs;
	    SeisTrcTranslator::getRanges( dbky_, cs );
	    ret = cs.zsamp_;
	}
    }
    return ret;
}


TrcKeySampling Seis::VolProvider::getHSampling() const
{
    TrcKeySampling ret;
    if ( fetcher_.dp_ )
	ret = fetcher_.dp_->sampling().hsamp_;
    else
    {
	if ( fetcher_.trl_ && !fetcher_.isMultiConn() )
	{
	    const SeisPacketInfo& pinfo = fetcher_.trl_->packetInfo();
	    ret.set( pinfo.inlrg, pinfo.crlrg );
	}
	else
	{
	    TrcKeyZSampling cs;
	    SeisTrcTranslator::getRanges( dbky_, cs );
	    ret = cs.hsamp_;
	}
    }
    return ret;
}


void Seis::VolProvider::getGeometryInfo( PosInfo::CubeData& cd ) const
{
    bool cdobtained = true;
    if ( fetcher_.dp_ )
	fetcher_.dp_->getTrcPositions( cd );
    else
    {
	if ( !fetcher_.trl_ )
	    cd.setEmpty();
	if ( fetcher_.isMultiConn() || !fetcher_.trl_->getGeometryInfo(cd) )
	    cdobtained = false;
    }
    if ( !cdobtained )
	cd.fillBySI( false );
}


void Seis::VolProvider::doUsePar( const IOPar& iop, uiRetVal& uirv )
{
    uirv.set( mTODONotImplPhrase() );
}


void Seis::VolProvider::doReset( uiRetVal& uirv ) const
{
    fetcher_.reset();
    uirv = fetcher_.uirv_;
}


void Seis::VolProvider::doGetNext( SeisTrc& trc, uiRetVal& uirv ) const
{
    fetcher_.getNext( trc );
    uirv = fetcher_.uirv_;
}


void Seis::VolProvider::doGet( const TrcKey& trcky, SeisTrc& trc,
				  uiRetVal& uirv ) const
{
    fetcher_.get( trcky, trc );
    uirv = fetcher_.uirv_;
}
