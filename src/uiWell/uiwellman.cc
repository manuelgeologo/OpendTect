/*+
________________________________________________________________________

 CopyRight:     (C) dGB Beheer B.V.
 Author:        Nanne Hemstra
 Date:          September 2003
 RCS:           $Id: uiwellman.cc,v 1.13 2004-02-02 15:23:18 nanne Exp $
________________________________________________________________________

-*/

#include "uiwellman.h"
#include "uiwelldlgs.h"
#include "iodirentry.h"
#include "ioobj.h"
#include "ioman.h"
#include "iostrm.h"
#include "ctxtioobj.h"
#include "wellman.h"
#include "welldata.h"
#include "welllogset.h"
#include "welllog.h"
#include "wellmarker.h"
#include "wellreader.h"
#include "wellwriter.h"
#include "welltransl.h"
#include "uiioobjmanip.h"
#include "uigroup.h"
#include "uilistbox.h"
#include "uitextedit.h"
#include "uibutton.h"
#include "uibuttongroup.h"
#include "uigeninputdlg.h"
#include "bufstringset.h"
#include "pixmap.h"
#include "filegen.h"
#include "strmprov.h"
#include "ptrman.h"
#include "uimsg.h"


static const int infoheight = 8;
static const int infowidth = 50;
static const int grpwidth = 75;


uiWellMan::uiWellMan( uiParent* p )
    : uiDialog(p,uiDialog::Setup("Well file management",
				 "Manage wells",
				 "107.1.0").nrstatusflds(1))
    , ctio(*mMkCtxtIOObj(Well))
    , markerschanged(this)
{
    IOM().to( ctio.ctxt.stdSelKey() );
    entrylist = new IODirEntryList( IOM().dirPtr(), ctio.ctxt );

    uiGroup* topgrp = new uiGroup( this, "Top things" );
    listfld = new uiListBox( topgrp, "Well objects" );
    listfld->setHSzPol( uiObject::medvar );
    for ( int idx=0; idx<entrylist->size(); idx++ )
	listfld->addItem( (*entrylist)[idx]->name() );
    listfld->setCurrentItem(0);
    listfld->selectionChanged.notify( mCB(this,uiWellMan,selChg) );

    manipgrp = new uiIOObjManipGroup( listfld, *entrylist, "well" );

    logsfld = new uiListBox( topgrp, "Available logs" );
    logsfld->attach( rightTo, manipgrp );
    logsfld->setToolTip( "Available logs" );

    butgrp = new uiButtonGroup( topgrp, "" );
    butgrp->attach( rightTo, logsfld );
    const ioPixmap renpm( GetDataFileName("renameobj.png") );
    uiToolButton* renbut = new uiToolButton( butgrp, "Rename", renpm );
    renbut->activated.notify( mCB(this,uiWellMan,renameLogPush) );
    renbut->setToolTip( "Rename selected log" );
    const ioPixmap rempm( GetDataFileName("trashcan.png") );
    rembut = new uiToolButton( butgrp, "Remove", rempm );
    rembut->activated.notify( mCB(this,uiWellMan,removeLogPush) );
    rembut->setToolTip( "Remove selected log" );

    uiPushButton* markerbut = new uiPushButton( this, "Edit markers ..." );
    markerbut->activated.notify( mCB(this,uiWellMan,addMarkers) );
    markerbut->attach( alignedBelow, topgrp );

    uiPushButton* logsbut = new uiPushButton( this, "Add logs ..." );
    logsbut->activated.notify( mCB(this,uiWellMan,addLogs) );
    logsbut->attach( rightOf, markerbut );

    infofld = new uiTextEdit( this, "File Info", true );
    infofld->attach( alignedBelow, markerbut );
    infofld->setPrefHeightInChar( infoheight );
    infofld->setPrefWidthInChar( infowidth );
    topgrp->setPrefWidthInChar( grpwidth );

    selChg( this );
    setCancelText( "" );
}


uiWellMan::~uiWellMan()
{
    delete ctio.ioobj; delete &ctio;
}


void uiWellMan::selChg( CallBacker* cb )
{
    entrylist->setCurrent( listfld->currentItem() );
    const IOObj* selioobj = entrylist->selected();
    ctio.setObj( selioobj ? selioobj->clone() : 0 );

    mkFileInfo();
    manipgrp->selChg( cb );
    fillLogsFld();

    BufferString msg;
    GetFreeMBOnDiskMsg( GetFreeMBOnDisk(ctio.ioobj), msg );
    toStatusBar( msg );
}


#define mInit() \
    if ( !ctio.ioobj ) return; \
    mDynamicCastGet(const IOStream*,iostrm,ctio.ioobj) \
    if ( !iostrm ) return; \
    BufferString fname( iostrm->fileName() ); \
    if ( !File_isAbsPath(fname) ) \
    { \
	fname = IOObjContext::getDataDirName(IOObjContext::WllInf); \
	fname = File_getFullPath( fname, iostrm->fileName() ); \
    } \
 \
    PtrMan<Well::Data> well = new Well::Data; \
    Well::Reader rdr( fname, *well );


void uiWellMan::fillLogsFld()
{
    mInit()
    BufferStringSet lognms;
    rdr.getLogInfo( lognms );

    logsfld->empty();
    for ( int idx=0; idx<lognms.size(); idx++)
	logsfld->addItem( lognms.get(idx) );
    logsfld->selAll( false );
}


#define mErrRet(msg) \
{ uiMSG().error(msg); return; }


void uiWellMan::addMarkers( CallBacker* )
{
    mInit()
    if ( !rdr.getD2T() )
	mErrRet( "Cannot add markers without depth to time model" );

    rdr.getMarkers();
    uiMarkerDlg dlg( this );
    dlg.setMarkerSet( well->markers() );
    if ( !dlg.go() ) return;

    dlg.getMarkerSet( well->markers() );
    Well::Writer wtr( fname, *well );
    wtr.putMarkers();
    const MultiID& key = ctio.ioobj->key();
    Well::MGR().reload( key );
    markerschanged.trigger();
}


void uiWellMan::addLogs( CallBacker* )
{
    mInit()
    if ( !rdr.getD2T() )
	mErrRet( "Cannot add logs without depth to time model" );

    rdr.getLogs();
    uiLoadLogsDlg dlg( this, *well );
    if ( !dlg.go() ) return;

    Well::Writer wtr( fname, *well );
    wtr.putLogs();
    fillLogsFld();
    const MultiID& key = ctio.ioobj->key();
    Well::MGR().reload( key );
}


void uiWellMan::removeLogPush( CallBacker* )
{
    if ( !logsfld->size() || !logsfld->nrSelected() ) return;

    if ( !uiMSG().askGoOn("This log will be removed from disk."
			  "\nDo you wish to continue?") )
	return;

    mInit()
    rdr.getLogs();
    Well::Log* log = well->logs().remove( logsfld->currentItem() );
    if ( strcmp(log->name(),logsfld->getText()) )
	return;

    delete log;
    if ( rdr.removeAll(Well::IO::sExtLog) )
    {
	Well::Writer wtr( fname, *well );
	wtr.putLogs();
	fillLogsFld();
    }

    const MultiID& key = ctio.ioobj->key();
    Well::MGR().reload( key );
}


void uiWellMan::renameLogPush( CallBacker* )
{
    if ( !logsfld->size() || !logsfld->nrSelected() ) mErrRet("No log selected")

    mInit()
    const int lognr = logsfld->currentItem() + 1;
    BufferString basenm = File_removeExtension( fname );
    BufferString logfnm = Well::IO::mkFileName( basenm, Well::IO::sExtLog, 
	    					lognr );
    StreamProvider sp( logfnm );
    StreamData sdi = sp.makeIStream();
    bool res = rdr.addLog( *sdi.istrm );
    sdi.close();
    if ( !res ) 
	mErrRet("Cannot read selected log")

    Well::Log& log = well->logs().getLog( 0 );

    BufferString titl( "Rename '" );
    titl += log.name(); titl += "'";
    uiGenInputDlg dlg( this, titl, "New name",
    			new StringInpSpec(log.name()) );
    if ( !dlg.go() ) return;

    BufferString newnm = dlg.text();
    if ( logsfld->isPresent(newnm) )
	mErrRet("Name already in use")

    log.setName( newnm );
    Well::Writer wtr( fname, *well );
    StreamData sdo = sp.makeOStream();
    wtr.putLog( *sdo.ostrm, log );
    sdo.close();
    fillLogsFld();
    const MultiID& key = ctio.ioobj->key();
    Well::MGR().reload( key );
}


void uiWellMan::mkFileInfo()
{
    if ( !ctio.ioobj )
    {
	infofld->setText( "" );
	return;
    }

    BufferString txt;
    mDynamicCastGet(const IOStream*,iostrm,ctio.ioobj)
    if ( !iostrm ) return;
    BufferString fname( iostrm->fileName() );
    if ( !File_isAbsPath(fname) )
    {
	fname = IOObjContext::getDataDirName(IOObjContext::WllInf);
	fname = File_getFullPath( fname, iostrm->fileName() );
    }

    txt += "File location: "; txt += File_getPathOnly( fname );
    txt += "\nFile name: "; txt += File_getFileName( fname );
    txt += "\nFile size: "; txt += getFileSize( fname );

    infofld->setText( txt );
}


BufferString uiWellMan::getFileSize( const char* filenm )
{
    BufferString szstr;
    double totalsz = (double)File_getKbSize( filenm );

    if ( totalsz > 1024 )
    {
        bool doGb = totalsz > 1048576;
        int nr = doGb ? mNINT(totalsz/10485.76) : mNINT(totalsz/10.24);
        szstr += nr/100;
        int rest = nr%100;
        szstr += rest < 10 ? ".0" : "."; szstr += rest;
        szstr += doGb ? " (Gb)" : " (Mb)";
    }
    else if ( !totalsz )
    {
        szstr += File_isEmpty(filenm) ? "-" : "< 1 (kB)";
    }
    else
    { szstr += totalsz; szstr += " (kB)"; }

    return szstr;
}
