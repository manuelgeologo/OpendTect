/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:        Bert
 Date:          Nov 2011
________________________________________________________________________

-*/
static const char* rcsID = "$Id: oddlsite.cc,v 1.8 2011-11-28 14:10:23 cvsbert Exp $";

#include "oddlsite.h"
#include "odhttp.h"
#include "httptask.h"
#include "databuf.h"
#include "file.h"
#include "filepath.h"
#include "strmoper.h"
#include "strmprov.h"
#include "settings.h"
#include "executor.h"
#include "thread.h"
#include "timefun.h"

static const char* sKeyTimeOut = "Download.Timout";


ODDLSite::ODDLSite( const char* h, float t )
    : odhttp_(*new ODHttp)
    , timeout_(t)
    , databuf_(0)
    , islocal_(matchString("DIR=",h))
    , isfailed_(false)
{
    host_ = h + (islocal_ ? 4 : (matchString("http://",h) ? 7 : 0));

    if ( timeout_ <= 0 )
    {
	Settings::common().get( sKeyTimeOut, timeout_ );
	if ( timeout_ <= 0 )
	    timeout_ = 5; // internet is quick these days
    }

    if ( host_.isEmpty() )
	host_ = "opendtect.org";
    if ( !islocal_ )
	odhttp_.requestFinished.notify( mCB(this,ODDLSite,reqFinish) );

    reConnect();
}


ODDLSite::~ODDLSite()
{
    delete databuf_;
    delete &odhttp_;
}


void ODDLSite::setTimeOut( float t, bool sett )
{
    timeout_ = t;
    if ( sett )
    {
	Settings::common().set( sKeyTimeOut, timeout_ );
	Settings::common().write();
    }
}


bool ODDLSite::reConnect()
{
    if ( islocal_ )
	return !(isfailed_ = !File::isDirectory(host_));

    if ( odhttp_.state() == ODHttp::Unconnected )
	odhttp_.setHost( host_ );

    Time::Counter tc; tc.start();
    while ( odhttp_.state() < ODHttp::Sending )
    {
	Threads::sleep( 0.1 );
	if ( tc.elapsed() > 1000 * timeout_ )
	{
	    errmsg_ = "Cannot open connection to ";
	    errmsg_.add( host_ ).add ( ":\n" );
	    if ( odhttp_.state() == ODHttp::HostLookup )
		errmsg_.add ( "Host name lookup timeout" );
	    if ( odhttp_.state() == ODHttp::Connecting )
		errmsg_.add ( "Host doesn't respond" );
	    else
		errmsg_.add ( "Internet connection not available" );
	    isfailed_ = true;
	    return false;
	}
    }
    return true;
}


bool ODDLSite::getFile( const char* relfnm, const char* outfnm )
{
    delete databuf_; databuf_ = 0;

    if ( islocal_ )
	return getLocalFile( relfnm, outfnm );

    HttpTask task( odhttp_ );
    odhttp_.get( getFileName(relfnm), outfnm );
    if ( !task.execute() )
	return false;

    if ( outfnm && *outfnm )
    {
	if ( !File::exists(outfnm) )
	{
	    errmsg_ = BufferString( "No output file: ", outfnm );
	    return false;
	}

	return true;
    }

    const od_int64 nrbytes = odhttp_.bytesAvailable();
    databuf_ = new DataBuffer( nrbytes, 1, true );
    const char* buffer = odhttp_.readCharBuffer();
    memcpy( databuf_->data(), buffer, nrbytes );
    return true;
}


bool ODDLSite::getLocalFile( const char* relfnm, const char* outfnm )
{
    const BufferString inpfnm( getFileName(relfnm) );

    if ( outfnm && *outfnm )
	return File::copy( inpfnm, outfnm );

    StreamData sd( StreamProvider(inpfnm).makeIStream() );
    if ( !sd.usable() )
	{ errmsg_ = "Cannot open "; errmsg_ += inpfnm; return false; }
    BufferString bs;
    const bool isok = StrmOper::readFile( *sd.istrm, bs );
    sd.close();
    if ( isok )
    {
	databuf_ = new DataBuffer( bs.size(), 1 );
	memcpy( (char*)databuf_->data(), bs.buf(), databuf_->size() );
    }
    return isok;
}


DataBuffer* ODDLSite::obtainResultBuf()
{
    DataBuffer* ret = databuf_;
    databuf_ = 0;
    return ret;
}


class ODDLSiteMultiFileGetter : public Executor
{
public:

ODDLSiteMultiFileGetter( ODDLSite& dls,
		const BufferStringSet& fnms, const char* outputdir )
    : Executor("File download")
    , dlsite_(dls)
    , fnms_(fnms)
    , curidx_(-1)
    , outdir_(outputdir)
    , httptask_(0)
{
    if ( !dlsite_.isOK() )
	{ msg_ = dlsite_.errMsg(); return; }
    if ( !File::isDirectory(outputdir) )
	{ msg_ = "Output directory does not exist"; return; }

    curidx_ = 0;
    if ( fnms_.isEmpty() )
	msg_ = "No files to get";
    else
	msg_.add( "Getting '" ).add( fnms_.get(curidx_) );

    if ( !dlsite_.islocal_ )
	httptask_ = new HttpTask( dlsite_.odhttp_ );
}

~ODDLSiteMultiFileGetter()
{
    delete httptask_;
}

const char* message() const	{ return msg_; }
const char* nrDoneText() const	{ return "Files downloaded"; }
od_int64 nrDone() const		{ return curidx_ + 1; }
od_int64 totalNr() const	{ return fnms_.size(); }

    int				nextStep();

    ODDLSite&			dlsite_;
    HttpTask*			httptask_;
    const BufferStringSet&	fnms_;
    int				curidx_;
    BufferString		outdir_;
    BufferString		msg_;

};


int ODDLSiteMultiFileGetter::nextStep()
{
    if ( curidx_ < 0 )
	return ErrorOccurred();
    else if ( curidx_ >= fnms_.size() )
	return Finished();

    const BufferString inpfnm( dlsite_.getFileName(fnms_.get(curidx_)) );
    BufferString outfnm( FilePath(outdir_).add(inpfnm).fullPath() );
    bool isok = true;
    if ( dlsite_.islocal_ )
	isok = dlsite_.getFile( inpfnm, outfnm );
    else
    {
	dlsite_.odhttp_.get( inpfnm, outfnm );
	isok = dlsite_.odhttp_.state() > ODHttp::Connecting;
    }

    if ( isok )
	curidx_++;
    else
    {
	msg_ = dlsite_.errMsg();
	curidx_ = -1;
    }

    return MoreToDo();
}


bool ODDLSite::getFiles( const BufferStringSet& fnms, const char* outputdir,
			 TaskRunner& tr )
{
    errmsg_.setEmpty();

    ODDLSiteMultiFileGetter mfg( *this, fnms, outputdir );
    if ( !tr.execute(mfg) )
    {
	errmsg_ = mfg.curidx_ < 0 ? mfg.msg_.buf() : "";
	return false;
    }

    const bool res = mfg.httptask_ ? tr.execute( *mfg.httptask_ ) : true;
    if ( !res && !mfg.httptask_->userStop() )
	errmsg_ = mfg.httptask_->message();

    return res;
}


void ODDLSite::reqFinish( CallBacker* )
{
    //TODO implement
}


BufferString ODDLSite::getFileName( const char* relfnm ) const
{
    if ( islocal_ )
    {
	FilePath fp( host_ );
	if ( !subdir_.isEmpty() )
	    fp.add( subdir_ );
	return fp.add(relfnm).fullPath();
    }

    BufferString ret( "/" );
    if ( subdir_.isEmpty() )
	ret.add( relfnm );
    else
	ret.add( FilePath( subdir_ ).add( relfnm ).fullPath(FilePath::Unix) );
    return ret;
}


BufferString ODDLSite::fullURL( const char* relfnm ) const
{
    if ( islocal_ )
	return getFileName( relfnm );

    BufferString ret( "http://" );
    ret.add( host_ ).add( "/" ).add( getFileName(relfnm) );
    return ret;
}
