/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:        N. Hemstra / Bert
 Date:          March 2003 / Feb 2008
________________________________________________________________________

-*/

#include "uiattribcrossplot.h"

#include "attribdesc.h"
#include "attribdescset.h"
#include "attribengman.h"
#include "attribsel.h"
#include "attribstorprovider.h"
#include "attribparam.h"
#include "datacoldef.h"
#include "datapointset.h"
#include "executor.h"
#include "ioobj.h"
#include "iopar.h"
#include "posinfo2d.h"
#include "posprovider.h"
#include "posfilterset.h"
#include "posvecdataset.h"
#include "seisioobjinfo.h"
#include "posinfo2dsurv.h"

#include "mousecursor.h"
#include "uidatapointset.h"
#include "uiioobjsel.h"
#include "uilistbox.h"
#include "uimsg.h"
#include "uiposfilterset.h"
#include "uiposprovider.h"
#include "uitaskrunner.h"
#include "uiseislinesel.h"
#include "od_helpids.h"

using namespace Attrib;

uiAttribCrossPlot::uiAttribCrossPlot( uiParent* p, const Attrib::DescSet& d )
	: uiDialog(p,uiDialog::Setup(tr("Attribute Cross-plotting"),mNoDlgTitle,
			mODHelpKey(mAttribCrossPlotHelpID)).modal(false))
	, ads_(*new Attrib::DescSet(d.is2D()))
	, lnmfld_(0)
	, curdps_(0)
	, dpsdispmgr_(0)
	, attrinfo_(0)
{
    uiGroup* attrgrp = new uiGroup( this, "Attribute group" );
    uiListBox::Setup asu( OD::ChooseZeroOrMore, uiStrings::sAttribute(mPlural),
			  uiListBox::AboveMid );
    attrsfld_ = new uiListBox( attrgrp, asu );

    if ( ads_.is2D() )
    {
	attrsfld_->itemChosen.notify(
		mCB(this,uiAttribCrossPlot,attrChecked) );
	attrsfld_->selectionChanged.notify(
		mCB(this,uiAttribCrossPlot,attrChanged) );

	uiListBox::Setup lsu( OD::ChooseAtLeastOne, tr("Line(s)"),
			      uiListBox::AboveMid );
	lnmfld_ = new uiListBox( attrgrp, lsu );
	lnmfld_->attach( rightTo, attrsfld_ );
	lnmfld_->itemChosen.notify(
		mCB(this,uiAttribCrossPlot,lineChecked) );
    }

    uiGroup* provgrp = new uiGroup( this, "Attribute group" );
    provgrp->attach( leftAlignedBelow, attrgrp );
    uiPosProvider::Setup psu( ads_.is2D(), true, true );
    psu.seltxt( uiStrings::phrSelect(tr("locations by")) )
       .choicetype( uiPosProvider::Setup::All );
    posprovfld_ = new uiPosProvider( provgrp, psu );
    posprovfld_->setExtractionDefaults();

    uiPosFilterSet::Setup fsu( ads_.is2D() );
    fsu.seltxt( tr("Location filters") ).incprovs( true );
    posfiltfld_ = new uiPosFilterSetSel( provgrp, fsu );
    posfiltfld_->attach( alignedBelow, posprovfld_ );

    setDescSet( d );
    postFinalise().notify( mCB(this,uiAttribCrossPlot,initWin) );
}


void uiAttribCrossPlot::setDescSet( const Attrib::DescSet& newads )
{
    const_cast<Attrib::DescSet&>(ads_) = newads;
    adsChg();
}


void uiAttribCrossPlot::adsChg()
{
    attrsfld_->setEmpty();

    delete attrinfo_;
    attrinfo_ = new Attrib::SelInfo( ads_ );
    for ( int idx=0; idx<attrinfo_->attrnms_.size(); idx++ )
    {
	const Attrib::Desc* desc = ads_.getDesc( attrinfo_->attrids_[idx] );
	if ( desc && desc->is2D() && desc->isPS() )
	{
	    attrinfo_->attrnms_.removeSingle( idx );
	    attrinfo_->attrids_.removeSingle( idx );
	    idx--;
	    continue;
	}
	attrsfld_->addItem( toUiString(attrinfo_->attrnms_.get(idx)) );
	attrsfld_->setChosen( attrsfld_->size()-1, true );
    }

    for ( int idx=0; idx<attrinfo_->ioobjids_.size(); idx++ )
    {
	SeisIOObjInfo sii( DBKey( attrinfo_->ioobjids_.get(idx) ) );
	if ( sii.isPS() && sii.is2D() )
	{
	    attrinfo_->ioobjids_.removeSingle( idx );
	    attrinfo_->ioobjnms_.removeSingle( idx );
	    idx--;
	    continue;
	}

	const char* ioobjnm = attrinfo_->ioobjnms_.get(idx).buf();
	attrsfld_->addItem( toUiString("[%1]").arg(ioobjnm) );
    }

    if ( !attrsfld_->isEmpty() )
	attrsfld_->setCurrentItem( int(0) );

    attrChanged( 0 );
}


uiAttribCrossPlot::~uiAttribCrossPlot()
{
    delete const_cast<Attrib::DescSet*>(&ads_);
    deepErase( dpsset_ );
}


void uiAttribCrossPlot::initWin( CallBacker* )
{
}


DBKey uiAttribCrossPlot::getSelectedID() const
{
    const int curitem = attrsfld_->currentItem();
    if ( !attrinfo_ || curitem<0 )
	return DBKey::getInvalid();

    const int attrsz = attrinfo_->attrnms_.size();
    const bool isstored = curitem >= attrsz;
    if ( isstored )
    {
	const int ioobjidx = curitem - attrsz;
	if ( attrinfo_->ioobjids_.validIdx(ioobjidx) )
	    return attrinfo_->ioobjids_[ioobjidx];

	return DBKey::getInvalid();
    }
    else
    {
	TypeSet<DescID>& descids = attrinfo_->attrids_;
	Attrib::DescID descid = descids.validIdx(curitem) ? descids[curitem]
							  : Attrib::DescID();
	const Attrib::Desc* desc = ads_.getDesc( descid );
	if ( !desc )
	    return DBKey::getInvalid();

	if ( desc->isPS() )
	{
	    BufferString psidstr;
	    mGetStringFromDesc( (*desc), psidstr, "id" );
	    return DBKey( psidstr );
	}

	return desc->getStoredID(true);
    }
}


void uiAttribCrossPlot::getLineNames( BufferStringSet& linenames )
{
    const SeisIOObjInfo seisinfo( getSelectedID() );
    seisinfo.getLineNames( linenames );
}


void uiAttribCrossPlot::lineChecked( CallBacker* )
{
    const int selitem = selidxs_.indexOf( attrsfld_->currentItem() );
    if ( selitem < 0 )
	return;

    linenmsset_[selitem].erase();
    for ( int lidx=0; lidx<lnmfld_->size(); lidx++ )
    {
	if ( lnmfld_->isChosen(lidx) )
	    linenmsset_[selitem].addIfNew( lnmfld_->itemText(lidx) );
    }
}


void uiAttribCrossPlot::attrChecked( CallBacker* )
{
    const DBKey selid = getSelectedID();
    const bool ischked = attrsfld_->isChosen( attrsfld_->currentItem() );
    if ( ischked && selidxs_.addIfNew(attrsfld_->currentItem()) )
    {
	selids_ += selid;
	linenmsset_ += BufferStringSet();
	const int lsidx = linenmsset_.size()-1;
	NotifyStopper ns( lnmfld_->selectionChanged );
	NotifyStopper ns2( lnmfld_->itemChosen );
	for ( int lidx=0; lidx<lnmfld_->size(); lidx++ )
	{
	    lnmfld_->setChosen( lidx, true );
	    linenmsset_[lsidx].add( lnmfld_->itemText(lidx) );
	}
    }
    else if ( !ischked )
    {
	const int selitem = selidxs_.indexOf( attrsfld_->currentItem() );
	if ( selitem >= 0 )
	{
	    selidxs_.removeSingle( selitem );
	    selids_.removeSingle( selitem );
	    NotifyStopper ns( lnmfld_->selectionChanged );
	    NotifyStopper ns2( lnmfld_->itemChosen );
	    for ( int lidx=0; lidx<lnmfld_->size(); lidx++ )
		lnmfld_->setChosen( lidx, false );

	    linenmsset_.removeSingle( selitem );
	}
    }
}


void uiAttribCrossPlot::attrChanged( CallBacker* )
{
    if ( !lnmfld_ ) return;

    NotifyStopper notifystop( lnmfld_->selectionChanged );
    NotifyStopper notifystop2( lnmfld_->itemChosen );
    lnmfld_->setEmpty();

    BufferStringSet linenames; getLineNames( linenames );

    for ( int lidx=0; lidx<linenames.size(); lidx++ )
    {
	lnmfld_->addItem( toUiString(linenames.get(lidx)) );
	lnmfld_->setChosen( lidx, false );
    }
}


#define mDPM DPM(DataPackMgr::PointID())

#define mErrRet(s) \
{ \
    if ( !s.isEmpty() ) uiMSG().error(s); \
    return false; \
}

bool uiAttribCrossPlot::acceptOK()
{
    RefMan<DataPointSet> dps = 0;
    PtrMan<Pos::Provider> prov = posprovfld_->createProvider();
    if ( !prov )
	mErrRet(toUiString("Internal: no Pos::Provider"))

    mDynamicCastGet(Pos::Provider2D*,p2d,prov.ptr())
    BufferStringSet linenames;
    if ( lnmfld_ )
    {
	for ( int lsidx=0; lsidx<selids_.size(); lsidx++ )
	{
	    PtrMan<IOObj> lsobj = selids_[lsidx].getIOObj();
	    if ( !lsobj )
		continue;

	    BufferStringSet lnms = linenmsset_[lsidx];
	    linenames.add( lnms, false );
	    for ( int lidx=0; lidx<lnms.size(); lidx++ )
	    {
		const auto geomid = SurvGeom::getGeomID(lnms.get(lidx));
		if ( geomid.isValid() )
		    p2d->addGeomID( geomid );
	    }
	}
    }

    uiTaskRunnerProvider trprov( this );
    if ( !prov->initialize( trprov ) )
	return false;

    ObjectSet<DataColDef> dcds;
    for ( int idx=0; idx<attrsfld_->size(); idx++ )
    {
	if ( attrsfld_->isChosen(idx) )
	    dcds += new DataColDef( attrsfld_->itemText(idx) );
    }

    if ( dcds.isEmpty() )
	mErrRet(uiStrings::phrPlsSelectAtLeastOne(tr("attribute to evaluate")))

    MouseCursorManager::setOverride( MouseCursor::Wait );
    IOPar iop; posfiltfld_->fillPar( iop );
    PtrMan<Pos::Filter> filt = Pos::Filter::make( iop, prov->is2D() );
    MouseCursorManager::restoreOverride();
    if ( filt && !filt->initialize(trprov) )
	return false;

    MouseCursorManager::setOverride( MouseCursor::Wait );
    dps = new DataPointSet( prov->is2D() );
    if ( !dps->extractPositions(*prov,dcds,trprov,filt) )
	return false;

    MouseCursorManager::restoreOverride();
    if ( dps->isEmpty() )
	mErrRet(tr("No positions selected"))

    uiPhrase dpsnm; prov->getSummary( dpsnm );
    if ( filt )
    {
	uiPhrase filtsumm; filt->getSummary( filtsumm );
	dpsnm.appendPhrase(toUiString("/ %1").arg(filtsumm),
				    uiString::Space, uiString::OnSameLine);
    }
    dps->setName( mFromUiStringTodo(dpsnm) );
    IOPar descsetpars;
    ads_.fillPar( descsetpars );
    const_cast<PosVecDataSet*>( &(dps->dataSet()) )->pars() = descsetpars;
    mDPM.add( dps );

    uiRetVal uirv; Attrib::EngineMan aem;
    MouseCursorManager::setOverride( MouseCursor::Wait );
    PtrMan<Executor> tabextr = aem.getTableExtractor( *dps, ads_, uirv );
    MouseCursorManager::restoreOverride();
    if ( !uirv.isOK() )
	mErrRet(uirv)

    if ( !trprov.execute( *tabextr ) )
	return false;

    uiDataPointSet* uidps = new uiDataPointSet( this, *dps,
		uiDataPointSet::Setup(tr("Attribute data"),false),dpsdispmgr_ );
    dpsset_ += uidps;
    IOPar& attrpar = uidps->storePars();
    ads_.fillPar( attrpar );
    if ( attrpar.name().isEmpty() )
	attrpar.setName( "Attributes" );

    return uidps->go() ? true : false;
}


const DataPointSet& uiAttribCrossPlot::getDPS() const
{ return *curdps_; }
