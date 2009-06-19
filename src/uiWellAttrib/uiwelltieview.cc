/*+
________________________________________________________________________

 CopyRight:	(C) dGB Beheer B.V.
 Author:	Bruno
 Date:		Jan 2009
________________________________________________________________________

-*/
static const char* rcsID = "$Id: uiwelltieview.cc,v 1.22 2009-06-19 17:00:14 cvsbruno Exp $";

#include "uiwelltieview.h"

#include "uiaxishandler.h"
#include "uidialog.h"

#include "uigraphicsscene.h"
#include "uigraphicsitemimpl.h"
#include "uiflatviewer.h"
#include "uifunctiondisplay.h"
#include "uilabel.h"
#include "uirgbarraycanvas.h"
#include "uitabstack.h"
#include "uiwelllogdisplay.h"

#include "arraynd.h"
#include "arrayndimpl.h"
#include "linear.h"
#include "flatposdata.h"
#include "geometry.h"
#include "iostrm.h"
#include "unitofmeasure.h"
#include "posinfo.h"
#include "position.h"
#include "posvecdataset.h"
#include "seistrc.h"
#include "seisbufadapters.h"
#include "welldata.h"
#include "welllogset.h"
#include "welllog.h"
#include "welld2tmodel.h"
#include "wellmarker.h"

#include "welltiedata.h"
#include "welltiegeocalculator.h"
#include "welltiepickset.h"
#include "welltiesetup.h"
#include "welltieunitfactors.h"



uiWellTieView::uiWellTieView( uiParent* p, uiFlatViewer* vwr, 
			      WellTieDataHolder* dhr,
			      ObjectSet<uiWellLogDisplay>* ldis )  
	: wd_(*dhr->wd()) 
	, vwr_(vwr)  
	, logsdisp_(*ldis)	     
	, dataholder_(dhr)  
	, params_(dhr->dpms())     	
	, wtsetup_(dhr->setup())	
    	, data_(*dhr->dispData())
	, datamgr_(*dhr->datamgr())
	, synthpickset_(dhr->pickmgr()->getSynthPickSet())
	, seispickset_(dhr->pickmgr()->getSeisPickSet())
	, trcbuf_(0)
	, checkshotitm_(0)
{
    initFlatViewer();
    initLogViewers();
} 


uiWellTieView::~uiWellTieView()
{
    delete trcbuf_;
}


void uiWellTieView::fullRedraw()
{
    setLogsParams();
    drawVelLog();
    drawDenLog();
    drawAILog();
    drawRefLog();
    drawTraces();
    drawWellMarkers();
    drawUserPicks();
    drawCShot();
    for ( int idx =0; idx<logsdisp_.size(); idx++ )
	logsdisp_[idx]->dataChanged();
    vwr_->handleChange( FlatView::Viewer::Annot );    
}



void uiWellTieView::initLogViewers()
{
    for ( int idx=0; idx<logsdisp_.size(); idx++ )
    {
	logsdisp_[idx]->setPrefWidth( vwr_->prefHNrPics()/2 );
	logsdisp_[idx]->setPrefHeight( vwr_->prefVNrPics() );
    }
    logsdisp_[0]->attach( leftOf, logsdisp_[1] );
    logsdisp_[1]->attach( leftOf, vwr_ );
}


void uiWellTieView::initFlatViewer()
{
    BufferString nm("Synthetics<------------------------------------>Seismics");
    vwr_->rgbCanvas().enableScrollZoom();
    vwr_->setInitialSize( uiSize(490,540) );
    vwr_->viewChanged.notify( mCB(this,uiWellTieView,zoomChg) );
    FlatView::Appearance& app = vwr_->appearance();
    app.setGeoDefaults( true );
    app.setDarkBG( false );
    app.annot_.color_ = Color::Black();
    app.annot_.setAxesAnnot( true );
    app.annot_.showaux_ = true ;
    app.annot_.x1_.showannot_ = true;
    app.annot_.x1_.showgridlines_ = false;
    app.annot_.x2_.showannot_ = true;
    app.annot_.x2_.sampling_ = 0.2;
    app.annot_.title_ = nm;
    app.ddpars_.show( true, false );
    app.ddpars_.wva_.right_= Color(255,0,0);
    app.ddpars_.wva_.left_= Color(0,0,255);
    app.ddpars_.wva_.clipperc_.set(0,0);
    app.ddpars_.wva_.wigg_ = Color::Black();
    app.ddpars_.wva_.overlap_ = 1;
}


void uiWellTieView::setLogsParams()
{
    for ( int idx =0; idx<logsdisp_.size(); idx++ )
	logsdisp_[idx]->setZDispInFeet( dataholder_->uipms()->iszinft_ );
    setLogsRanges( params_->dptintv_.start, params_->dptintv_.stop );
}


void uiWellTieView::drawVelLog()
{
    uiWellLogDisplay::LogData& wldld1 = logsdisp_[0]->logData( true );
    wldld1.wl_ = wd_.logs().getLog( params_->dispcurrvellognm_ );
    wldld1.xrev_ = !wtsetup_.issonic_;
    wldld1.linestyle_.color_ = Color::stdDrawColor(0);
}


void uiWellTieView::drawDenLog()
{
    uiWellLogDisplay::LogData& wldld2 = logsdisp_[0]->logData( false );
    wldld2.wl_ = wd_.logs().getLog( params_->denlognm_ );
    wldld2.linestyle_.color_ = Color::stdDrawColor(1);
}


void uiWellTieView::drawAILog()
{
    uiWellLogDisplay::LogData& wldld1 = logsdisp_[1]->logData( true );
    wldld1.wl_ = wd_.logs().getLog( params_->ainm_ );
    wldld1.linestyle_.color_ = Color::stdDrawColor(0);
}


void uiWellTieView::drawRefLog()
{
    uiWellLogDisplay::LogData& wldld2 = logsdisp_[1]->logData( false );
    wldld2.wl_ = wd_.logs().getLog( params_->refnm_ );
    wldld2.xrev_ = true;
    wldld2.linestyle_.color_ = Color::stdDrawColor(1);
}


void uiWellTieView::drawTraces()
{
    if ( !trcbuf_ )
	trcbuf_ = new SeisTrcBuf(true);
    trcbuf_->erase();

    setUpTrcBuf( trcbuf_, params_->synthnm_, 5 );
    setUpTrcBuf( trcbuf_, params_->attrnm_, 5 );
    setDataPack( trcbuf_, params_->attrnm_, 5 );
}


void uiWellTieView::setUpTrcBuf( SeisTrcBuf* trcbuf, const char* varname, 
				  int nrtraces )
{
    const int varsz = data_.getLength();
    SeisTrc valtrc;
    SeisTrc udftrc;

    valtrc.reSize( varsz, false );
    udftrc.reSize( varsz, false ) ;

    setUpValTrc( valtrc, varname, varsz );
    setUpUdfTrc( udftrc, varname, varsz );

    for ( int idx=0; idx<nrtraces+2; idx++ )
    {
	bool isudf =  ( idx<1 || idx > nrtraces );
	SeisTrc* newtrc = new SeisTrc( isudf? udftrc : valtrc );
	trcbuf->add( newtrc );
	trcbuf->get(trcbuf->size()-1)->info().nr = trcbuf->size()-1;
    }
}


void uiWellTieView::setUpUdfTrc( SeisTrc& trc, const char* varname, int varsz )
{
    for ( int idx=0; idx<varsz; idx++)
	trc.set( idx, mUdf(float), 0 );
}


void uiWellTieView::setUpValTrc( SeisTrc& trc, const char* varname, int varsz )
{
    for ( int idx=0; idx<varsz; idx++)
    {
	float val = data_.get( varname, idx );
	trc.set( idx, val, 0 );
    }
}


void uiWellTieView::setDataPack( SeisTrcBuf* trcbuf, const char* varname, 
				 int vwrnr )
{   
    const int type = trcbuf->get(0)->info().getDefaultAxisFld( 
			    Seis::Line, &trcbuf->get(1)->info() );
    SeisTrcBufDataPack* dp =
	new SeisTrcBufDataPack( trcbuf, Seis::Line, 
				(SeisTrcInfo::Fld)type, "Seismic" );
    dp->trcBufArr2D().setBufMine( false );

    DPM(DataPackMgr::FlatID()).add( dp );
    StepInterval<double> zrange( params_->timeintv_.start, 
	    			 params_->timeintv_.stop,
				 params_->timeintv_.step*params_->step_ );
    StepInterval<double> xrange( 1, trcbuf->size(), 1 );
    dp->posData().setRange( false, zrange );
    dp->posData().setRange( true, xrange );
    dp->setName( varname );
    
    FlatView::Appearance& app = vwr_->appearance();
    
    vwr_->setPack( true, dp->id(), false, true );
    vwr_->handleChange( FlatView::Viewer::All );
    const UnitOfMeasure* uom = 0;
    const char* units =  ""; //uom ? uom->symbol() : "";
    app.annot_.x1_.name_ =  varname;
    app.annot_.x2_.name_ = "TWT (s)";
}


void uiWellTieView::setLogsRanges( float start, float stop )
{
    for (int idx=0; idx<logsdisp_.size(); idx++)
	logsdisp_[idx]->setZRange( Interval<float>( start, stop) );
}


void uiWellTieView::removePack()
{
    const TypeSet<DataPack::ID> ids = vwr_->availablePacks();
    for ( int idx=ids.size()-1; idx>=0; idx-- )
	DPM( DataPackMgr::FlatID() ).release( ids[idx] );
}


void uiWellTieView::zoomChg( CallBacker* )
{
    const Well::D2TModel* d2tm = wd_.d2TModel();
    if ( !d2tm ) return; 

    uiWorldRect curwr = vwr_->curView();
    const float start = curwr.top();
    const float stop  = curwr.bottom();
    setLogsRanges( d2tm->getDepth(start), d2tm->getDepth(stop) );
}


void uiWellTieView::drawMarker( FlatView::Annotation::AuxData* auxdata,
       				int vwridx, float xpos,  float zpos,
			       	Color col, bool ispick )
{
    FlatView::Appearance& app = vwr_->appearance();
    app.annot_.auxdata_ +=  auxdata;
    auxdata->linestyle_.color_ = col;
    auxdata->linestyle_.width_ = 2;
    auxdata->linestyle_.type_  = LineStyle::Solid;
    
    const float xleft = vwr_->boundingBox().left();
    const float xright = vwr_->boundingBox().right();

    if ( xpos && xpos < ( xright-xleft )/2 )
    {
	auxdata->poly_ += FlatView::Point( (xright-xleft)/2, zpos );
	auxdata->poly_ += FlatView::Point( xleft, zpos );
    }
    else if ( xpos && xpos>( xright-xleft )/2 )
    {
	auxdata->poly_ += FlatView::Point( xright, zpos );
	auxdata->poly_ += FlatView::Point( (xright-xleft)/2, zpos );
    }
}	


void uiWellTieView::drawWellMarkers()
{
    deepErase( wellmarkerauxdatas_ );
    const Well::D2TModel* d2tm = wd_.d2TModel();
    if ( !d2tm ) return; 
   
    for ( int midx=0; midx<wd_.markers().size(); midx++ )
    {
	Well::Marker* marker = const_cast<Well::Marker*>( 
						wd_.markers()[midx] );
	if ( !marker  ) continue;
	
	float zpos = d2tm->getTime( marker->dah() ); 
	const Color col = marker->color();
	
	if ( zpos < params_->timeintv_.start || zpos > params_->timeintv_.stop 
		|| col == Color::NoColor() || col.rgb() == 16777215 )
	    continue;
	
	FlatView::Annotation::AuxData* auxdata = 0;
	mTryAlloc( auxdata, FlatView::Annotation::AuxData(marker->name()) );
	wellmarkerauxdatas_ += auxdata;
	
	//drawMarker( auxdata, 0, 0, zpos, col, false );
    }
    bool ismarkerdisp = dataholder_->uipms()->ismarkerdisp_;
    for ( int idx=0; idx<logsdisp_.size(); idx++ )
	logsdisp_[idx]->setMarkers( ismarkerdisp ? &wd_.markers() : 0 );
}	


void uiWellTieView::drawUserPicks()
{
    deepErase( userpickauxdatas_ );
    const int nrauxs = mMAX(seispickset_->getSize(),synthpickset_->getSize());
    
    for ( int idx=0; idx<nrauxs; idx++ )
    {
	FlatView::Annotation::AuxData* auxdata = 0;
	mTryAlloc( auxdata, FlatView::Annotation::AuxData(0) );
	userpickauxdatas_ += auxdata;
    }
    
    const Well::D2TModel* d2tm = wd_.d2TModel();
    if ( !d2tm ) return; 

    for ( int idx=0; idx<seispickset_->getSize(); idx++ )
    {
	const UserPick* pick = seispickset_->get(idx);
	if ( !pick  ) continue;

	float zpos = d2tm->getTime( pick->dah_ ); 
	float xpos = pick->xpos_;
	
	drawMarker( userpickauxdatas_[idx], 0, xpos, zpos, 
		    pick->color_, false );

	uiWellLogDisplay::PickData* pd = 
			new uiWellLogDisplay::PickData( zpos,pick->color_);
	for ( int idx=0; idx<logsdisp_.size(); idx++ )
	   logsdisp_[idx]->zPicks() += *pd; 
    }

    for ( int idx=0; idx<synthpickset_->getSize(); idx++ )
    {
	const UserPick* pick = synthpickset_->get(idx);
	if ( !pick  ) continue;

	float zpos = d2tm->getTime( pick->dah_ ); 
	float xpos = pick->xpos_;
	
	drawMarker( userpickauxdatas_[idx], 0, xpos, zpos, 
		    pick->color_, false );
    }
}


void uiWellTieView::drawCShot()
{
    uiGraphicsScene& scene = logsdisp_[0]->scene();
    scene.removeItem( checkshotitm_ );
    delete checkshotitm_; checkshotitm_=0;
    if ( !dataholder_->uipms()->iscsdisp_ ) 
	return;
    const Well::D2TModel* cs = wd_.checkShotModel();
    if ( !cs  ) return;
    const int sz = cs->size();
    if ( sz<2 ) return;
    
    WellTieGeoCalculator geocalc( dataholder_->params(), &wd_ );

    TypeSet<float> csvals, cstolog, dpt;
    for ( int idx=0; idx<sz; idx++ )
    {
	float val = cs->value( idx );
	float dah = cs->dah( idx );
	csvals += val;
	dpt    += dah;
    }
    geocalc.TWT2Vel( csvals, dpt, cstolog, true );
    
    float maxval, minval = cstolog[0];
    for ( int idx=0; idx<sz; idx++ )
    {
	float val = cstolog[idx];
	if ( val > maxval )
	    maxval = val;
	if ( val < minval )
	    minval = val;
    }

    TypeSet<uiPoint> pts;

    uiWellLogDisplay::LogData& ld = logsdisp_[0]->logData();
    if ( minval<ld.valrg_.start )   
	minval = ld.valrg_.start;
    if ( maxval<ld.valrg_.stop )
	maxval = ld.valrg_.stop;

    Interval<float> dispvalrg( minval, maxval );
    Interval<float> zrg = logsdisp_[0]->zRange();
    ld.xax_.setBounds( dispvalrg );
    ld.yax_.setBounds( zrg );
    for ( int idx=0; idx<sz; idx++ )
    {
	float val = cstolog[idx];
	float dah = dpt[sz-idx-1];
	if ( dah < zrg.start )
	    continue;
	else if ( dah > zrg.stop )
	    break;
	
	if ( dataholder_->uipms()->iszinft_ ) dah *= mToFeetFactor;
	pts += uiPoint( ld.xax_.getPix(val), ld.yax_.getPix(dah) );
    }

    if ( pts.isEmpty() ) return;

    checkshotitm_ = scene.addItem( new uiPolyLineItem(pts) );
    LineStyle ls( LineStyle::Solid, 2, Color::DgbColor() );
    checkshotitm_->setPenStyle( ls );
}




uiWellTieCorrView::uiWellTieCorrView( uiParent* p, WellTieDataHolder* dh)
	: uiGroup(p)
    	, params_(*dh->dpms())  
	, corrdata_(*dh->corrData())
	, welltiedata_(dh->data())			
{
    uiFunctionDisplay::Setup fdsu; fdsu.border_.setRight( 0 );

    for (int idx=0; idx<1; idx++)
    {
	corrdisps_ += new uiFunctionDisplay( this, fdsu );
	corrdisps_[idx]->xAxis()->setName( "Lags (ms)" );
	corrdisps_[idx]->yAxis(false)->setName( "Coefficient" );
    }
    	
    corrlbl_ = new uiLabel( this,"" );
    corrlbl_->attach( centeredAbove, corrdisps_[0] );
}


uiWellTieCorrView::~uiWellTieCorrView()
{
    deepErase( corrdisps_ );
}


void uiWellTieCorrView::setCrossCorrelation()
{
    const int datasz = corrdata_.get(params_.crosscorrnm_)->info().getSize(0);
    
    const float corrcoeff = welltiedata_.corrcoeff_; 
    float scalefactor = corrcoeff/corrdata_.get(params_.crosscorrnm_,datasz/2);
    TypeSet<float> xvals,corrvals;
    for ( int idx=-datasz/2; idx<datasz/2; idx++)
    {
	xvals += idx*params_.timeintv_.step*params_.step_*1000;
	corrvals += corrdata_.get(params_.crosscorrnm_, idx+datasz/2)
	    	    	     *scalefactor;
    }


    for (int idx=0; idx<corrdisps_.size(); idx++)
	corrdisps_[idx]->setVals( xvals.arr(), corrvals.arr(), xvals.size() );
    
    BufferString corrbuf = "Cross-Correlation Coefficient: ";
    corrbuf += corrcoeff;
    corrlbl_->setPrefWidthInChar(50);
    corrlbl_->setText( corrbuf );
}

