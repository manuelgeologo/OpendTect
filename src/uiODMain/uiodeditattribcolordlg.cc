/*+
___________________________________________________________________

 CopyRight: 	(C) dGB Beheer B.V.
 Author: 	R. K. Singh
 Date: 		Jan 2008
 RCS:		$Id: uiodeditattribcolordlg.cc,v 1.1 2008-02-04 07:31:37 cvsraman Exp $
___________________________________________________________________

-*/

#include "uiodeditattribcolordlg.h"

#include "colortab.h"
#include "coltabedit.h"
#include "uibutton.h"
#include "uiodapplmgr.h"
#include "uiodattribtreeitem.h"
#include "uiodscenemgr.h"
#include "uivispartserv.h"
#include "viscolortab.h"


uiODEditAttribColorDlg::uiODEditAttribColorDlg( uiParent* p,
						ObjectSet<uiTreeItem>& set,
       						const char* attrnm )
    : uiDialog(p,uiDialog::Setup("Color Settings","",""))
    , items_(set)
{
    if ( attrnm && *attrnm )
    {
	BufferString titletext = "Color Settings : ";
	titletext += attrnm;
	setTitleText( titletext );
    }

    visBase::VisColorTab* ctabobj = 0;
    for ( int idx=0; idx<items_.size(); idx++ )
    {
	mDynamicCastGet(uiODAttribTreeItem*,item,items_[idx])
	if ( !item ) continue;

	const int id =
	    ODMainWin()->applMgr().visServer()->getColTabId( item->displayID(),
							    item->attribNr() );
	if ( id < 0 ) continue;
	mDynamicCastGet(visBase::VisColorTab*,obj,visBase::DM().getObject(id))
	if ( !obj ) continue;

	ctabobj = obj;
	break;
    }

    ColorTable ctab = ctabobj->colorSeq().colors();
    ctab.scaleTo( ctabobj->getInterval() );
    coltabed_ = new ColorTableEditor( this, ColorTableEditor::Setup()
	    			      .editable(true).withclip(true)
				      .vertical(true), &ctab );
    coltabed_->setPrefHeight( 400 );
    const bool autoscale = ctabobj->autoScale();
    coltabed_->setAutoScale( autoscale );
    coltabed_->setClipRate( ctabobj->clipRate() );
    coltabed_->setSymmetry( ctabobj->getSymmetry() );

    uiPushButton* applybut = new uiPushButton( this, "Apply", true );
    applybut->attach( centeredBelow, coltabed_ );
    applybut->activated.notify( mCB(this,uiODEditAttribColorDlg,doApply) );
}


void uiODEditAttribColorDlg::doApply( CallBacker* )
{
    ColorTable newct = *coltabed_->getColorTable();
    const bool autoscale = coltabed_->autoScale();
    const Interval<float> intv = coltabed_->getColorTable()->getInterval();
    const float cliprate = coltabed_->getClipRate();
    const bool symm = coltabed_->getSymmetry();
    for ( int idx=0; idx<items_.size(); idx++ )
    {
	mDynamicCastGet(uiODAttribTreeItem*,item,items_[idx])
	if ( !item ) continue;

	const int id = 
	    ODMainWin()->applMgr().visServer()->getColTabId( item->displayID(),
							    item->attribNr() );
	if ( id < 0 ) continue;
	mDynamicCastGet(visBase::VisColorTab*,obj,visBase::DM().getObject(id))
	if ( !obj ) continue;

	if ( !(obj->colorSeq().colors()==newct) )
	{
	    obj->colorSeq().colors() = newct;
	    obj->triggerSeqChange();
	}
	if ( obj->autoScale()!=autoscale || obj->clipRate()!=cliprate
	  				 || obj->getSymmetry()!=symm )
	{
	    obj->setAutoScale( autoscale );
	    obj->setClipRate( cliprate );
	    obj->setSymmetry( symm );
	    obj->triggerAutoScaleChange();
	}
	if ( !autoscale && obj->getInterval() != intv )
	{
	    obj->scaleTo( intv );
	    obj->triggerRangeChange();
	}

	items_[idx]->updateColumnText( uiODSceneMgr::cColorColumn() );
    }
}


bool uiODEditAttribColorDlg::acceptOK( CallBacker* )
{
    doApply( 0 );
    return true;
}

