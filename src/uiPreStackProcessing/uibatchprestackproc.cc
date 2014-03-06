/*+
 * (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 * AUTHOR   : K. Tingdahl
 * DATE     : April 2005
-*/

static const char* rcsID mUsedVar = "$Id$";

#include "uibatchprestackproc.h"

#include "ctxtioobj.h"
#include "ioman.h"
#include "keystrs.h"
#include "seistrctr.h"
#include "uipossubsel.h"
#include "uimsg.h"
#include "uiseissel.h"
#include "uibatchjobdispatchersel.h"
#include "uiprestackprocessorsel.h"
#include "prestackprocessor.h"

namespace PreStack
{

uiBatchProcSetup::uiBatchProcSetup( uiParent* p, bool is2d )
    : uiDialog(p,Setup("Prestack Processing",mNoDlgTitle,"103.2.10"))
    , outputctxt_( *uiSeisSel::mkCtxtIOObj( is2d ? Seis::LinePS : Seis::VolPS,
					    false ) )
    , inputctxt_( *uiSeisSel::mkCtxtIOObj( is2d ? Seis::LinePS : Seis::VolPS,
					    true ) )
    , is2d_( is2d )
{
    chainsel_ = new uiProcSel( this, "Setup", 0 );

    inputctxt_.ctxt.forread = true;
    inputsel_ = new uiSeisSel( this,
	       inputctxt_,uiSeisSel::Setup( is2d ? Seis::LinePS : Seis::VolPS));
    inputsel_->attach( alignedBelow, chainsel_ );

    possubsel_ =  new uiPosSubSel( this, uiPosSubSel::Setup(is2d,false) );
    possubsel_->attach( alignedBelow, inputsel_ );

    outputctxt_.ctxt.forread = false;
    outputsel_ = new uiSeisSel( this, outputctxt_,
		       uiSeisSel::Setup( is2d ? Seis::LinePS : Seis::VolPS ));
    outputsel_->attach( alignedBelow, possubsel_ );
    outputsel_->selectionDone.notify(
				 mCB(this,uiBatchProcSetup,outputNameChangeCB));

    batchfld_ = new uiBatchJobDispatcherSel( this, false,
					     Batch::JobSpec::PreStack );
    batchfld_->attach( alignedBelow, outputsel_ );

    outputNameChangeCB( 0 );
}


uiBatchProcSetup::~uiBatchProcSetup()
{
    delete inputctxt_.ioobj; delete &inputctxt_;
    delete outputctxt_.ioobj; delete &outputctxt_;
}


void uiBatchProcSetup::outputNameChangeCB( CallBacker* )
{
    const IOObj* ioobj = outputsel_->ioobj( true );
    if ( ioobj )
	batchfld_->setJobName( ioobj->name() );
}


bool uiBatchProcSetup::prepareProcessing()
{
    MultiID chainmid;
    PtrMan<IOObj> ioobj = 0;
    if ( chainsel_->getSel(chainmid) )
	ioobj = IOM().get( chainmid );

    if ( !ioobj )
    {
	uiMSG().error("Please select a processing setup");
	return false;
    }

    if ( !inputsel_->commitInput() )
    {
	uiMSG().error("Please select an input volume");
	return false;
    }

    if ( !outputsel_->commitInput() )
    {
	if ( outputsel_->isEmpty() )
	    uiMSG().error("Please enter an output name");
	return false;
    }

    return true;
}


bool uiBatchProcSetup::fillPar()
{
    IOPar& par = batchfld_->jobSpec().pars_;

    if ( !inputctxt_.ioobj || !outputctxt_.ioobj )
	return false;

    MultiID mid;
    if ( !chainsel_->getSel(mid) )
	return false;

    par.set( ProcessManager::sKeyInputData(),  inputctxt_.ioobj->key() );
    possubsel_->fillPar( par );
    par.set( ProcessManager::sKeyOutputData(), outputctxt_.ioobj->key() );
    //Set depthdomain in output's omf?

    par.set( ProcessManager::sKeySetup(), mid );

    Seis::GeomType geom = is2d_ ? Seis::LinePS : Seis::VolPS;
    Seis::putInPar( geom, par );
    return true;
}


bool uiBatchProcSetup::acceptOK( CallBacker* )
{
    return prepareProcessing() && fillPar() ? batchfld_->start() : false;
}


} // namespace PreStack
