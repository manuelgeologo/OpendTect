/*+
________________________________________________________________________

 CopyRight:     (C) dGB Beheer B.V.
 Author:        N. Hemstra
 Date:          April 2002
 RCS:           $Id: uislicesel.cc,v 1.35 2005-09-08 10:45:10 cvsnanne Exp $
________________________________________________________________________

-*/

#include "uislicesel.h"
#include "cubesampling.h"
#include "uispinbox.h"
#include "uicombobox.h"
#include "uigeninput.h"
#include "uibutton.h"
#include "survinfo.h"
#include "timer.h"
#include "thread.h"

static const char* sButTxtAdvance = "Advance >>";
static const char* sButTxtPause = "Pause";


uiSliceSel::uiSliceSel( uiParent* p, const CubeSampling& curcs_,
			const CubeSampling& maxcs_,
			const CallBack& acb, Type type )
    : uiDialog(p,uiDialog::Setup("Positioning",
				 "Specify the element's position",
				 "").modal(type==Vol||type==TwoD))
    , cs(*new CubeSampling(curcs_))
    , curcs(curcs_)
    , maxcs(maxcs_)
    , inl0fld(0)
    , updatemutex(*new Threads::Mutex)
    , appcb(*new CallBack(acb))
    , scrolldlg(0)
{
    isinl = type == Inl;
    iscrl = type == Crl;
    istsl = type == Tsl;
    isvol = type == Vol;
    is2d = type == TwoD;

    if ( !is2d )
	createInlFld();

    createCrlFld();
    createZFld();

    if ( inl0fld )
	mainObject()->setTabOrder( (uiObject*)inl0fld, (uiObject*)crl0fld );
    mainObject()->setTabOrder( (uiObject*)crl0fld, (uiObject*)z0fld );

    if ( !isvol && !is2d )
    {
	uiButton* applybut = new uiPushButton( this, "Apply" );
	applybut->activated.notify( mCB(this,uiSliceSel,applyPush) );
	mainObject()->setTabOrder( (uiObject*)z0fld, (uiObject*)applybut );
	applybut->attach( alignedBelow, z0fld );
	uiButton* scrollbut = new uiPushButton( this, "Scroll ..." );
	scrollbut->activated.notify( mCB(this,uiSliceSel,scrollPush) );
	scrollbut->attach( rightOf, isinl ? inl0fld : (iscrl?crl0fld:z0fld) );
    }
}


void uiSliceSel::createInlFld()
{
    Interval<int> inlrg( curcs.hrg.start.inl, curcs.hrg.stop.inl );
    StepInterval<int> maxinlrg( maxcs.hrg.start.inl, maxcs.hrg.stop.inl,
				maxcs.hrg.step.inl );
    BufferString label( isinl ? "Inline nr" : "Inline range" );
    inl0fld = new uiLabeledSpinBox( this, label );
    setBoxValues( inl0fld->box(), maxinlrg, inlrg.start );
    inl1fld = new uiSpinBox( this );
    setBoxValues( inl1fld, maxinlrg, inlrg.stop );
    inl1fld->attach( rightTo, inl0fld );
    inl1fld->display( !isinl );
}


void uiSliceSel::createCrlFld()
{
    Interval<int> crlrg( curcs.hrg.start.crl, curcs.hrg.stop.crl );
    StepInterval<int> maxcrlrg( maxcs.hrg.start.crl, maxcs.hrg.stop.crl,
				maxcs.hrg.step.crl );
    BufferString label = is2d ? "Trace range" 
			      : ( iscrl ? "Xline nr" : "Xline range" );
    crl0fld = new uiLabeledSpinBox( this, label );
    setBoxValues( crl0fld->box(), maxcrlrg, crlrg.start );
    crl1fld = new uiSpinBox( this );
    setBoxValues( crl1fld, maxcrlrg, crlrg.stop );
    crl1fld->attach( rightTo, crl0fld );
    crl1fld->display( !iscrl );
    if ( inl0fld ) crl0fld->attach( alignedBelow, inl0fld );
}


void uiSliceSel::createZFld()
{
    const float zfact( SI().zFactor() );
    Interval<int> zrg( mNINT(curcs.zrg.start*zfact), 
	    	       mNINT(curcs.zrg.stop*zfact) );
    BufferString label = SI().zIsTime() ? "Time " : "Depth ";
    if ( !istsl ) label += "range "; label += SI().getZUnit();
    StepInterval<int> maxzrg = 
		    StepInterval<int>( mNINT(maxcs.zrg.start*zfact),
				       mNINT(maxcs.zrg.stop*zfact),
				       mNINT(maxcs.zrg.step*zfact) );

    z0fld = new uiLabeledSpinBox( this, label );
    setBoxValues( z0fld->box(), maxzrg, zrg.start );
    z1fld = new uiSpinBox( this );
    setBoxValues( z1fld, maxzrg, zrg.stop );
    z1fld->attach( rightTo, z0fld );
    z1fld->display( !istsl );
    z0fld->attach( alignedBelow, crl0fld );
}


class uiSliceScroll : public uiDialog
{
public:

uiSliceScroll( uiSliceSel* ss )
	: uiDialog(ss,uiDialog::Setup("Scrolling",getTitle(ss),"0.4.2")
				      .modal(false))
	, slcsel(ss)
	, inauto(false)
	, paused(false)
	, zfact( SI().zFactor() )
{
    setCtrlStyle( LeaveOnly );
    timer = new Timer( "uiSliceScroll timer" );
    timer->tick.notify( mCB(this,uiSliceScroll,timerTick) );

    const CubeSampling& cs = SI().sampling( false );
    const HorSampling& hs = cs.hrg;
    int step = hs.step.inl;
    int maxstep = hs.start.inl - hs.stop.inl;
    if  ( ss->iscrl )
    {
	step = hs.step.crl;
	maxstep = hs.start.crl - hs.stop.crl;
    }
    else if ( ss->istsl )
    {
	step = mNINT(cs.zrg.step*zfact);
	float zrg = (cs.zrg.stop - cs.zrg.start) * zfact;
	maxstep = mNINT(zrg);
    }
    if ( maxstep < 0 ) maxstep = -maxstep;
    stepfld = new uiLabeledSpinBox( this, "Scroll step" );
    stepfld->box()->setMinValue( -maxstep );
    stepfld->box()->setMaxValue( maxstep );
    stepfld->box()->setStep( step );
    stepfld->box()->setValue( step );

    typfld = new uiLabeledComboBox( this, "Control" );
    typfld->box()->addItem( "Manual" );
    typfld->box()->addItem( "Auto" );
    typfld->box()->selectionChanged.notify( mCB(this,uiSliceScroll,typSel) );
    typfld->attach( alignedBelow, stepfld );

    ctrlbut = new uiPushButton( this, sButTxtAdvance );
    ctrlbut->activated.notify( mCB(this,uiSliceScroll,butPush) );
    ctrlbut->attach( alignedBelow, typfld );
    backbut = new uiPushButton( this, "<< Step back" );
    backbut->activated.notify( mCB(this,uiSliceScroll,butPush) );
    backbut->attach( leftOf, ctrlbut );

    dtfld = new uiGenInput( this, "Time between updates (s)", FloatInpSpec(1) );
    dtfld->attach( alignedBelow, ctrlbut );

    finaliseDone.notify( mCB(this,uiSliceScroll,typSel) );
}


~uiSliceScroll()
{
    delete timer; timer = 0;
}


void typSel( CallBacker* )
{
    const bool autoreq = typfld->box()->currentItem() == 1;
    dtfld->display( autoreq );
    if ( inauto != autoreq )
    {
	if ( autoreq )
	    startAuto();
	else
	    stopAuto( false );
    }
    ctrlbut->setText( autoreq ? sButTxtPause : sButTxtAdvance );
    backbut->display( !autoreq );
    inauto = autoreq;
}


void butPush( CallBacker* cb )
{
    if ( !inauto )
	doAdvance( cb != ctrlbut );
    else
    {
	/*new*/paused = *ctrlbut->text() == 'P';
	ctrlbut->setText( paused ? "Go" : sButTxtPause );
    }
}


void startAuto()
{
    paused = false;
    doAdvance( false );
    ctrlbut->setText( sButTxtPause );
    setTimer();
}

void stopAuto( bool setmanual )
{
    timer->stop();
    inauto = false;
    if ( setmanual )
    {
	typfld->box()->setCurrentItem( 0 );
	ctrlbut->setText( sButTxtAdvance );
	backbut->display( true );
    }
}


void doAdvance( bool reversed )
{
    if ( !timer ) return;

    const int step = (reversed ? -1 : 1) * stepfld->box()->getValue();
    slcsel->readInput();
    if ( slcsel->isinl )
    {
	int newval = slcsel->cs.hrg.start.inl + step;
	if ( !SI().sampling(true).hrg.inlOK(newval) )
	    stopAuto( true );
	else
	    slcsel->inl0fld->box()->setValue( newval );
    }
    else if ( slcsel->iscrl )
    {
	int newval = slcsel->cs.hrg.start.crl + step;
	if ( !SI().sampling(true).hrg.crlOK(newval) )
	    stopAuto( true );
	else
	    slcsel->crl0fld->box()->setValue( newval );
    }
    else
    {
	const float zfac = SI().zFactor();
	float newval = slcsel->cs.zrg.start + step / zfac;
	if ( !SI().sampling(true).zrg.includes(newval) )
	    stopAuto( true );
	else
	{
	    if ( zfac < 10 )
		slcsel->z0fld->box()->setValue( newval );
	    else
	    {
		newval *= zfac;
		slcsel->z0fld->box()->setValue( mNINT(newval) );
	    }
	}
    }

    slcsel->applyPush(0);
}


void timerTick( CallBacker* )
{
    if ( !inauto )
	return;
    if ( !paused )
	doAdvance( false );
    setTimer();
}


void setTimer()
{
    if ( !timer ) return;

    float val = dtfld->getfValue();
    if ( Values::isUdf(val) || val < 0.2 )
	val = 200;
    else
	val *= 1000;
    timer->start( mNINT(val), true );
}


bool rejectOK( CallBacker* )
{
    paused = true;
    inauto = false;
    return true;
}


const char* getTitle( uiSliceSel* ss )
{
    static BufferString title;
    title = "Control scrolling through ";
    if ( !ss->istsl )
	title += ss->isinl ? "Inlines" : "Crosslines";
    else
    {
	title += SI().zIsTime() ? "Time" : "Depth";
	title += " slices";
    }
    return title.buf();
}

    uiSliceSel*		slcsel;
    uiLabeledSpinBox*	stepfld;
    uiLabeledComboBox*	typfld;
    uiPushButton*	ctrlbut;
    uiPushButton*	backbut;
    uiGenInput*		dtfld;
    const float		zfact;

    bool		paused;
    bool		inauto;
    Timer*		timer;

};


uiSliceSel::~uiSliceSel()
{
    delete &cs;
    delete &appcb;
    delete &updatemutex;
    delete scrolldlg;
}


void uiSliceSel::setBoxValues( uiSpinBox* box, const StepInterval<int>& intv, 
			       int curval )
{
    box->setMinValue( intv.start );
    box->setMaxValue( intv.stop );
    box->setStep( intv.step, true );
    box->setValue( curval );
}


void uiSliceSel::scrollPush( CallBacker* )
{
    if ( !scrolldlg )
	scrolldlg = new uiSliceScroll( this );
    scrolldlg->show();
}


void uiSliceSel::applyPush( CallBacker* )
{
    if ( !updatemutex.tryLock() )
	return;
    readInput();
    appcb.doCall(this);
    updatemutex.unlock();
}


void uiSliceSel::readInput()
{
    const HorSampling& hs = SI().sampling(true).hrg;
    Interval<int> inlrg, crlrg;
    hs.get( inlrg, crlrg );
    if ( inl0fld )
    {
	inlrg.start = inl0fld->box()->getValue();
	inlrg.stop = isinl ? inlrg.start : inl1fld->getValue();
	if ( !isinl && inlrg.start == inlrg.stop )
	    inlrg.stop += hs.step.inl;
    }

    crlrg.start = crl0fld->box()->getValue();
    crlrg.stop = iscrl ? crlrg.start : crl1fld->getValue();
    if ( !iscrl && crlrg.start == crlrg.stop )
	crlrg.stop += hs.step.crl;

    Interval<float> zrg;
    zrg.start = z0fld->box()->getValue() / SI().zFactor();
    SI().snapZ( zrg.start, 0 );
    if ( istsl )
	zrg.stop = zrg.start;
    else
    {
	zrg.stop = z1fld->getValue() / SI().zFactor();
	zrg.sort();
	SI().snapZ( zrg.stop, 1 );
	if ( mIsEqual(zrg.start,zrg.stop,mDefEps) )
	    zrg.stop += SI().zStep();
    }

    cs.hrg.set( inlrg, crlrg );
    assign(cs.zrg,zrg);

    SI().snap( cs.hrg.start, BinID(0,0) );
    SI().snap( cs.hrg.stop, BinID(0,0) );
}


bool uiSliceSel::acceptOK( CallBacker* )
{
    readInput();
    return true;
}
