#_______________________Pmake___________________________________________________
#
#	Makefile : 	Seismics
# 	July 1995	Bert
#_______________________________________________________________________________


set(OD_MODULE_DEPS Geometry MMProc )

set(OD_NIGHTLY_TEST_PROGS
	seisblocks.cc
	seisstorer.cc
	seisprovider.cc
	seisblocksdataglueer.cc
)

set(OD_MODULE_SOURCES
	initseis.cc
	offsetazimuth.cc
	prestackgather.cc
	segydirect.cc
	segydirect2d.cc
	segydirecttr.cc
	segyfiledata.cc
	segyfiledef.cc
	segyhdr.cc
	segyhdrcalc.cc
	segyhdrdef.cc
	segyresorter.cc
	segyscanner.cc
	segytr.cc
	seis2ddata.cc
	seis2ddatafmtconverter.cc
	seis2dlineio.cc
	seis2dto3d.cc
	seisbayesclass.cc
	seisblocks.cc
	seisblocksdataglueer.cc
	seisblockshdf5backend.cc
	seisblocksreader.cc
	seisblockswriter.cc
	seisblocksstreambackend.cc
	seisblockstr.cc
	seisbuf.cc
	seiscbvs.cc
	seiscbvs2d.cc
	seisconvolve.cc
	seiscubeimpfromothersurv.cc
	seiscbvsps.cc
	seiscopy.cc
	seismscprov.cc
	seiscube2linedata.cc
	seisdatapack.cc
	seisdatapackwriter.cc
	seisdatapackfromdps.cc
	seiseventsnapper.cc
	seisfetcher.cc
	seisfixedcubeprov.cc
	seisimpbpsif.cc
	seisimporter.cc
	seisimpps.cc
	seisinfo.cc
	seisioobjinfo.cc
	seisiosimple.cc
	seisjobexecprov.cc
	seiskeytracker.cc
	seislineprovider.cc
	seisloader.cc
	seismerge.cc
	seismulticubeps.cc
	seismultiprovider.cc
	seisposindexer.cc
	seisposprovider.cc
	seispreload.cc
	seisprovider.cc
	seisprovidertester.cc
	seisps2dprovider.cc
	seisps3dprovider.cc
	seispsioprov.cc
	seispsmerge.cc
	seisrandlineto2d.cc
	seisrangeseldata.cc
	seisrawtrcsseq.cc
	seisresampler.cc
	seisseldata.cc
	seisseldataimpl.cc
	seissingtrcproc.cc
	seisstatscalc.cc
	seisstatscollector.cc
	seisstorer.cc
	seistrc.cc
	seistrcprop.cc
	seistrctr.cc
	seisvolprovider.cc
	seiszaxisstretcher.cc
	synthseisdataset.cc
	synthseisgenerator.cc
	synthseisgenparams.cc
	timedepthconv.cc
	wavelet.cc
	waveletio.cc
	waveletattrib.cc
	waveletextractor.cc
	waveletmanager.cc
)

set(OD_MODULE_BATCHPROGS
	    od_copy_seis.cc
	    od_process_2dto3d.cc
	    od_process_time2depth.cc
	    od_process_segyio.cc
)

set ( OD_MODULE_PROGS od_SeisMan.cc )

OD_INIT_MODULE()
