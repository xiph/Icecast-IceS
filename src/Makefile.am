## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

if HAVE_OSS

ossheaders = im_oss.h
osssources = im_oss.c

endif

if HAVE_SUN_AUDIO

sunheaders = im_sun.h
sunsources = im_sun.c

endif

SUBDIRS = avl thread net log timing

bin_PROGRAMS = ices

noinst_HEADERS = config.h input.h inputmodule.h im_playlist.h signals.h stream.h reencode.h encode.h playlist_basic.h logging.h im_stdinpcm.h $(ossheaders) $(sunheaders) event.h stream_shared.h metadata.h audio.h resample.h
ices_SOURCES = input.c config.c stream.c ices.c signals.c im_playlist.c reencode.c encode.c playlist_basic.c im_stdinpcm.c $(osssources) $(sunsources) stream_shared.c savefile.c metadata.c stream_rewrite.c playlist_script.c audio.c resample.c

ices_LDADD = net/libicenet.la thread/libicethread.la log/libicelog.la\
	avl/libiceavl.la timing/libicetiming.la

LIBS = @LIBS@ -lpthread @SOCKET_LIBS@ @XML_LIBS@ @OGG_LIBS@ @VORBIS_LIBS@\
       @VORBISENC_LIBS@ @SHOUT_LIBS@

CFLAGS = @CFLAGS@ @XML_CFLAGS@ @OGG_CFLAGS@ @VORBIS_CFLAGS@ @SHOUT_CFLAGS@

INCLUDES = -Inet -Ithread -Iavl -Ilog -Itiming $(OSS_CFLAGS) $(SUN_CFLAGS)

# SCCS stuff (for BitKeeper)
GET = true

debug:
	$(MAKE) all CFLAGS="@DEBUG@ @XML_CFLAGS@ @OGG_CFLAGS@ @VORBIS_CFLAGS@ @SHOUT_CFLAGS@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@ @XML_CFLAGS@ @OGG_CFLAGS@ @VORBIS_CFLAGS@ @SHOUT_CFLAGS@"

