#pragma once

/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:	Bert
 Date:		May 2018
________________________________________________________________________


-*/

#include "wellreadaccess.h"
#include "welldahobj.h"
#include "hdf5reader.h"
#include "uistring.h"
class IOObj;


namespace Well
{

class HDF5Writer;


/*!\brief stuff common to HDF5 well reader and writer  */

mExpClass(Well) HDF5Access
{
public:

    typedef DahObj::size_type	size_type;
    typedef DahObj::idx_type	idx_type;

    static const char*		sTrackDSName();
    static const char*		sLogsDSName();
    static const char*		sMarkersDSName();
    static const char*		sD2TDSName();
    static const char*		sCSMdlDSName();

};


/*!\brief Reads Well::Data from HDF5 file  */

mExpClass(Well) HDF5Reader : public ReadAccess
			   , public HDF5Access
{ mODTextTranslationClass(Well::HDF5Reader)
public:

			HDF5Reader(const IOObj&,Data&,uiString& errmsg);
			HDF5Reader(const char* fnm,Data&,uiString& errmsg);
			HDF5Reader(const HDF5Writer&,Data&,uiString& errmsg);
			~HDF5Reader();

    virtual bool	getInfo() const;
    virtual bool	getTrack() const;
    virtual bool	getLogs() const;
    virtual bool	getMarkers() const;
    virtual bool	getD2T() const;
    virtual bool	getCSMdl() const;
    virtual bool	getDispProps() const;
    virtual bool	getLog(const char* lognm) const;
    virtual void	getLogNames(BufferStringSet&) const;
    virtual void	getLogInfo(ObjectSet<IOPar>&) const;

    virtual const uiString& errMsg() const	{ return errmsg_; }

protected:

    uiString&		errmsg_;
    HDF5::Reader*	rdr_;

    void		init(const char*);

};

}; // namespace Well
