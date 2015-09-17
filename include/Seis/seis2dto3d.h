#ifndef seis2dto3d_h
#define seis2dto3d_h


/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:        Bruno
 Date:          Feb 2011
 RCS:           $Id$
________________________________________________________________________

-*/


#include "seismod.h"
#include "executor.h"
#include "trckeyzsampling.h"
#include "arrayndimpl.h"
#include "fourier.h"
#include "binid.h"
#include "seisbuf.h"
#include "uistring.h"
#include "od_ostream.h"

class IOObj;
class Seis2DDataSet;
class SeisScaler;
class SeisTrc;
class SeisTrcWriter;
class SeisTrcBuf;
class od_ostream;



mExpClass(Seis) Seis2DTo3D : public Executor
{ mODTextTranslationClass(Seis2DTo3D)
public:

			Seis2DTo3D( od_ostream&, TaskRunner* );
			~Seis2DTo3D();

    uiString		uiMessage() const
			{ return errmsg_.isEmpty() ? tr("interpolating")
						   : errmsg_; }
    od_int64		nrDone() const		{ return nrdone_; }
    uiString		uiNrDoneText() const	{ return tr("Done"); }
    od_int64		totalNr() const;
    int			nextStep();

    bool		init(const IOPar&);

    static const char*	sKeyInput();
    static const char*	sKeyPow();
    static const char*	sKeyTaper();
    static const char*	sKeySmrtScale();

protected:
    bool		usePar(const IOPar&);
    bool		setIO(const IOPar&);
    bool		checkParameters();

    IOObj*		inioobj_;
    IOObj*		outioobj_;
    TrcKeyZSampling	tkzs_;

    uiString		errmsg_;

    SeisTrcBuf&		seisbuf_;
    TrcKeySampling	seisbuftks_;

    SeisTrcWriter*      wrr_;
    SeisTrcReader*	rdr_;

    SeisTrcBuf		tmpseisbuf_;

    bool		read_;
    int			nrdone_;
    mutable int		totnr_;
    bool		read();

    //everything below added by Dirk
    Array3D<float_complex>*		trcarr_;
    Array3D<float_complex>*		butterfly_;
    Array3D<float_complex>*		geom_;

    od_ostream&		strm_;

    Fourier::CC*	fft_;

    bool		smartscaling_;

    float		rmsmax_;
    float		pow_;
    TaskRunner*		tr_;

    float		taperangle_;
    bool		readData();
    void		readInputCube(const int szfastx,
				      const int szfasty, const int szfastz );
    void		butterflyOperator();
    void		multiplyArray( const Array3DImpl<float_complex>& a,
				       Array3DImpl<float_complex>& b);
    bool		scaleArray();
    void		smartScale();
    bool		writeOutput();


};
#endif


