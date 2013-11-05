/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:        N. Hemstra
 Date:          April 2002
________________________________________________________________________

-*/
static const char* rcsID mUsedVar = "$Id$";

#include "uiviszstretchdlg.h"

#include "iopar.h"
#include "pixmap.h"
#include "survinfo.h"
#include "uibutton.h"
#include "uicombobox.h"
#include "uilabel.h"
#include "uislider.h"
#include "visdataman.h"
#include "vistransmgr.h"
#include "vissurvscene.h"
#include <typeinfo>


uiZStretchDlg::uiZStretchDlg( uiParent* p )
    : uiDialog(p,
	       uiDialog::Setup("Z Scaling","Set scaling factor","50.0.7")
	       .canceltext(""))
    , valchgd_(false)
    , vwallbut_(0)
    , scenefld_(0)
    , sliderfld_(0)
{
    visBase::DM().getIDs( typeid(visSurvey::Scene), sceneids_ );
    if ( sceneids_.size() == 0 )
    {
	new uiLabel( this, "No scenes available" );
	return;
    }

    if ( sceneids_.size() > 1 )
    {
	BufferStringSet scenenms;
	scenenms.add( "All" );
	for ( int idx=0; idx<sceneids_.size(); idx++ )
	{
	    mDynamicCastGet(visSurvey::Scene*,scene,
		    	    visBase::DM().getObject(sceneids_[idx]))
	    scenenms.add( scene->name() );
	}

	scenefld_ = new uiLabeledComboBox( this, scenenms, "Apply scaling to" );
	scenefld_->box()->setCurrentItem( 1 );
	mAttachCB( scenefld_->box()->selectionChanged, uiZStretchDlg::sceneSel);
    }

    sliderfld_ = new uiSliderExtra( this, uiSliderExtra::Setup("Z stretch")
				     .withedit(true).nrdec(3).logscale(true),
	   				"Z stretch slider" );
    mAttachCB( sliderfld_->sldr()->valueChanged,uiZStretchDlg::sliderMove );
    if ( scenefld_ )
	sliderfld_->attach( alignedBelow, scenefld_ );

    mAttachCB( preFinalise(), uiZStretchDlg::doFinalise );
}


void uiZStretchDlg::doFinalise( CallBacker* )
{
    updateSliderValues();

    if ( !vwallcb.willCall() && !homecb.willCall() )
	return;

    uiGroup* grp = new uiGroup( this, "icons" );
    if ( vwallcb.willCall() )
    {
	ioPixmap vwallpm( "view_all" );
	vwallbut_ = new uiPushButton( grp, "&Fit to scene", vwallpm, true );
	mAttachCB( vwallbut_->activated, uiZStretchDlg::butPush );
    }
    if ( homecb.willCall() )
    {
	ioPixmap homepm( "home" );
	uiButton* homebut = new uiPushButton( grp, "To &Home", homepm, true );
	mAttachCB( homebut->activated, uiZStretchDlg::butPush );
	if ( vwallbut_ )
	    homebut->attach( rightOf, vwallbut_ );
    }

    grp->attach( centeredBelow, sliderfld_ );
    savefld_ = new uiCheckBox( this, sSaveAsDefault() );
    savefld_->attach( alignedBelow, grp );
}


void uiZStretchDlg::sceneSel( CallBacker* )
{
    updateSliderValues();
}


void uiZStretchDlg::updateSliderValues()
{
    initslval_ = getCurrentZStretch();
    sliderfld_->sldr()->setMinValue( 0.04f*initslval_ );
    sliderfld_->sldr()->setMaxValue( 25*initslval_ );
    sliderfld_->sldr()->setValue( initslval_ );
}


float uiZStretchDlg::getCurrentZStretch() const
{
    if ( sceneids_.size() == 0 )
	return 1.0f;

    int sceneidx = scenefld_ ? scenefld_->box()->currentItem()-1 : 0;
    if ( sceneidx < 0 ) sceneidx = 0;
    mDynamicCastGet(visSurvey::Scene*,scene,
		    visBase::DM().getObject(sceneids_[sceneidx]));

    return scene->getFixedZStretch() * scene->getTempZStretch();
}


void uiZStretchDlg::setZStretch( float zstretch, bool permanent )
{
    const bool stretchall = scenefld_ && scenefld_->box()->currentItem()==0;
    for ( int idx=0; idx<sceneids_.size(); idx++ )
    {
	bool dostretch = !scenefld_ || stretchall ||
		       idx == scenefld_->box()->currentItem()-1;
	if ( !dostretch ) continue;

	mDynamicCastGet(visSurvey::Scene*,scene,
			visBase::DM().getObject(sceneids_[idx]));

	if ( permanent )
	{
	    MouseCursorChanger cursorchanger( MouseCursor::Busy );
	    scene->setTempZStretch( 1.f );
	    scene->setFixedZStretch( zstretch );
	}
	else
	{
	    scene->setTempZStretch( zstretch/scene->getFixedZStretch() );
	}
    }
}


bool uiZStretchDlg::acceptOK( CallBacker* )
{
    if ( !sliderfld_ )
	return true;

    sliderfld_->processInput();
    const float slval = sliderfld_->sldr()->getValue();
    setZStretch( slval, true );

    if ( savefld_->isChecked() )
    {
	SI().getPars().set( visSurvey::Scene::sKeyZStretch(), slval );
	SI().savePars();
    }

    if ( slval != initslval_ )
	valchgd_ = true;
    return true;
}


bool uiZStretchDlg::rejectOK( CallBacker* )
{
    setZStretch( initslval_, false );
    return true;
}


void uiZStretchDlg::sliderMove( CallBacker* )
{
    const float slval = sliderfld_->sldr()->getValue();
    setZStretch( slval, false );
}


void uiZStretchDlg::butPush( CallBacker* b )
{
    if ( b == vwallbut_ )
	vwallcb.doCall( this );
    else
	homecb.doCall( this );
}
