/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:        Raman K Singh
 Date:          Jul 2010
________________________________________________________________________

-*/

static const char* rcsID mUsedVar = "$Id$";

#include "uibasemap.h"
#include "uigraphicsitemimpl.h"
#include "uigraphicsscene.h"
#include "uigraphicsview.h"
#include "uipixmap.h"
#include "uirgbarray.h"
#include "uiworld2ui.h"

#include "survinfo.h"


uiBaseMapObject::uiBaseMapObject( BaseMapObject* bmo )
    : bmobject_( bmo )
    , itemgrp_(*new uiGraphicsItemGroup(true))
    , changed_(false)
{
    if ( bmobject_ )
    {
	bmobject_->changed.notify( mCB(this,uiBaseMapObject,changedCB) );
	bmobject_->stylechanged.notify(
		    mCB(this,uiBaseMapObject,changedStyleCB) );
	itemgrp_.setZValue( bmobject_->getDepth() );
    }
}


uiBaseMapObject::~uiBaseMapObject()
{
    delete &itemgrp_;
}


const char* uiBaseMapObject::name() const
{ return bmobject_ ? bmobject_->name().buf() : 0; }


void uiBaseMapObject::show( bool yn )
{ yn ? itemgrp_.show() : itemgrp_.hide(); }


void uiBaseMapObject::changedCB( CallBacker* )
{
    changed_ = true;
    update();
}


void uiBaseMapObject::changedStyleCB( CallBacker* )
{
    changed_ = true;
    updateStyle();
}


void uiBaseMapObject::update()
{
    if ( !bmobject_ ) return;

    Threads::Locker( bmobject_->lock_ );

    int itemnr = 0;
    for ( int idx=0; idx<bmobject_->nrShapes(); idx++ )
    {
	TypeSet<Coord> crds;
	bmobject_->getPoints( idx, crds );

	TypeSet<uiWorldPoint> worldpts;
	for ( int cdx=0; cdx<crds.size(); cdx++ )
	    worldpts += crds[cdx];

	if ( bmobject_->getLineStyle(idx) &&
	     bmobject_->getLineStyle(idx)->type_!=LineStyle::None )
	{
	    while ( itemgrp_.size()>itemnr )
	    {
		mDynamicCastGet(uiPolyLineItem*,itm,
				itemgrp_.getUiItem(itemnr));
		if ( !itm )
		    itemgrp_.remove( itemgrp_.getUiItem(itemnr), true );
		else break;
	    }

	    if ( itemgrp_.size()<=itemnr )
		itemgrp_.add( new uiPolyLineItem() );

	    mDynamicCastGet(uiPolyLineItem*,li,itemgrp_.getUiItem(itemnr))
	    if ( !li ) return;

	    li->setPenStyle( *bmobject_->getLineStyle(idx) );
	    li->setPolyLine( worldpts );
	    itemnr++;
	}

	if ( bmobject_->fill(idx) )
	{
	    while ( itemgrp_.size()>itemnr )
	    {
		mDynamicCastGet(uiPolygonItem*,itm,itemgrp_.getUiItem(itemnr));
		if ( !itm )
		    itemgrp_.remove( itemgrp_.getUiItem(itemnr), true );
		else break;
	    }

	    if ( itemgrp_.size()<=itemnr )
		itemgrp_.add( new uiPolygonItem() );

	    mDynamicCastGet(uiPolygonItem*,itm,itemgrp_.getUiItem(itemnr))
	    if ( !itm ) return;

	    itm->setPolygon( worldpts );
	    itm->fill();
	    itemnr++;
	}

	const MarkerStyle2D* ms2d = bmobject_->getMarkerStyle( idx );
	if ( ms2d && ms2d->type_!=MarkerStyle2D::None )
	{
	    for ( int ptidx=0; ptidx<crds.size(); ptidx++ )
	    {
		while ( itemgrp_.size()>itemnr )
		{
		    mDynamicCastGet(uiMarkerItem*,itm,
				    itemgrp_.getUiItem(itemnr));
		    if ( !itm )
			itemgrp_.remove( itemgrp_.getUiItem(itemnr), true );
		    else break;
		}

		if ( itemgrp_.size()<=itemnr )
		    itemgrp_.add( new uiMarkerItem() );

		mDynamicCastGet(uiMarkerItem*,itm,itemgrp_.getUiItem(itemnr));
		itm->setMarkerStyle( *ms2d );
		itm->setPenColor( ms2d->color_ );
		itm->setFillColor( ms2d->color_ );
		itm->setPos( crds[ptidx] );
		itemnr++;
	    }
	}

	const OD::RGBImage* rgbimage = bmobject_->getImage( idx );
	if ( rgbimage )
	{
	    while ( itemgrp_.size()<itemnr )
	    {
		mDynamicCastGet(uiPixmapItem*,itm,itemgrp_.getUiItem(itemnr));
		if ( !itm )
		    itemgrp_.remove( itemgrp_.getUiItem(itemnr), true );
		else break;
	    }

	    if ( itemgrp_.size()<=itemnr )
		itemgrp_.add( new uiPixmapItem() );

	    mDynamicCastGet(uiPixmapItem*,itm,itemgrp_.getUiItem(itemnr));

	    mDynamicCastGet(const uiRGBArray*,rgbarr,rgbimage)
	    uiPixmap pixmap( *rgbarr );
	    itm->setPixmap( pixmap );

	    itemnr++;
	}

	const char* shapenm = bmobject_->getShapeName( idx );
	if ( shapenm && !crds.isEmpty() )
	{
	    while ( itemgrp_.size()>itemnr )
	    {
		mDynamicCastGet(uiTextItem*,itm,
				itemgrp_.getUiItem(itemnr));
		if ( !itm )
		    itemgrp_.remove( itemgrp_.getUiItem(itemnr), true );
		else break;
	    }

	    if ( itemgrp_.size()<=itemnr )
		itemgrp_.add( new uiTextItem() );

	    mDynamicCastGet(uiTextItem*,itm,itemgrp_.getUiItem(itemnr));
	    itm->setText( shapenm );
	    itm->setPos( crds[0] );
	    Alignment al = bmobject_->getAlignment( idx );
	    itm->setAlignment( al );
	    itemnr++;
	}
    }

    while ( itemgrp_.size()>itemnr )
	itemgrp_.remove( itemgrp_.getUiItem(itemnr), true );
}


void uiBaseMapObject::updateStyle()
{
    if ( !bmobject_ ) return;

    Threads::Locker( bmobject_->lock_ );

    int itemnr = 0;
    for ( int idx=0; idx<bmobject_->nrShapes(); idx++ )
    {
	if ( bmobject_->getLineStyle(idx) &&
	     bmobject_->getLineStyle(idx)->type_!=LineStyle::None )
	{
	    mDynamicCastGet(uiPolyLineItem*,li,itemgrp_.getUiItem(itemnr))
	    if ( !li ) return;
	    li->setPenStyle( *bmobject_->getLineStyle(idx) );
	    itemnr++;
	}

	if ( bmobject_->fill(idx) )
	{
	    mDynamicCastGet(uiPolygonItem*,itm,itemgrp_.getUiItem(itemnr))
	    if ( !itm ) return;

	    itm->fill();
	    itemnr++;
	}

	const char* shapenm = bmobject_->getShapeName( idx );
	if ( shapenm )
	    itemnr++;
    }
}



// uiBaseMap
uiBaseMap::uiBaseMap( uiParent* p )
    : uiGroup(p,"Basemap")
    , view_(*new uiGraphicsView(this,"Basemap"))
    , w2ui_(*new uiWorld2Ui)
    , worlditemgrp_(*new uiGraphicsItemGroup(true))
    , changed_(false)
{
    view_.scene().addItem( &worlditemgrp_ );
    view_.reSize.notify( mCB(this,uiBaseMap,reSizeCB) );
}


uiBaseMap::~uiBaseMap()
{
    deepErase( objects_ );
    view_.scene().removeItem( &worlditemgrp_ );
    delete &view_;
    delete &w2ui_;
}


void uiBaseMap::reSizeCB( CallBacker* )
{
    updateTransform();
}


void uiBaseMap::setView( const uiWorldRect& wr )
{
    wr_ = wr;
    updateTransform();
}


void uiBaseMap::updateTransform()
{
    const uiRect viewrect( 0, 0, (int)view_.scene().width(),
				 (int)view_.scene().height() );

    if ( mIsZero(wr_.width(),mDefEps) || mIsZero(wr_.height(),mDefEps) )
	return;

    w2ui_.set( viewrect, wr_ );

    const double xscale = viewrect.width()/(wr_.right()-wr_.left());
    const double yscale = viewrect.height()/(wr_.bottom()-wr_.top());
    const double xpos = viewrect.left()-xscale*wr_.left();
    const double ypos = viewrect.top()-yscale*wr_.top();

    worlditemgrp_.setPos( uiWorldPoint(xpos,ypos) );
    worlditemgrp_.setScale( (float)xscale, (float)yscale );
    reDraw();
}


void uiBaseMap::addObject( BaseMapObject* obj )
{
    const int index = indexOf( obj );
    if ( index==-1 )
    {
	uiBaseMapObject* uiobj = new uiBaseMapObject( obj );
	addObject( uiobj );
    }
    else
    {
	objects_[index]->update();
    }
}


bool uiBaseMap::hasChanged()
{
    if ( changed_ ) return true;

    for ( int idx=0; idx<objects_.size(); idx++ )
	if ( objects_[idx]->hasChanged() ) return true;

    return false;
}


void uiBaseMap::resetChangeFlag()
{
    changed_ = false;

    for ( int idx=0; idx<objects_.size(); idx++ )
	objects_[idx]->resetChangeFlag();
}


void uiBaseMap::addObject( uiBaseMapObject* uiobj )
{
    if ( !uiobj ) return;

    worlditemgrp_.add( &uiobj->itemGrp() );
    objects_ += uiobj;
    changed_ = true;
}


void uiBaseMap::show( const BaseMapObject& obj, bool yn )
{
    const int objidx = indexOf( &obj );
    if ( !objects_.validIdx(objidx) ) return;

    objects_[objidx]->show( yn );
}


int uiBaseMap::indexOf( const BaseMapObject* obj ) const
{
    for ( int idx=0; idx<objects_.size(); idx++ )
    {
	if ( objects_[idx]->bmobject_==obj )
	{
	    return idx;
	}
    }

    return -1;
}


void uiBaseMap::removeObject( const BaseMapObject* obj )
{
    const int index = indexOf( obj );
    if ( index==-1 )
    {
	pErrMsg( "Base map object not found" );
    }

    worlditemgrp_.remove( &objects_[index]->itemGrp(), true );
    delete objects_.removeSingle( index );
    changed_ = true;
}


void uiBaseMap::reDraw( bool )
{
    for ( int idx=0; idx<objects_.size(); idx++ )
	objects_[idx]->update();
}


const char* uiBaseMap::nameOfItemAt( const Geom::Point2D<int>& pt )  const
{
    const uiGraphicsItem* itm = view_.scene().itemAt( pt );
    if ( !itm ) return 0;

    for ( int idx=0; idx<objects_.size(); idx++ )
    {
	const uiGraphicsItemGroup& itmgrp = objects_[idx]->itemGrp();
	if ( !itmgrp.isPresent(*itm) )
	    continue;

	return objects_[idx]->name();
    }

    return 0;
}


