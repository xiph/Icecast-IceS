## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

SUBDIRS = log

bin_PROGRAMS = ices
AM_CFLAGS = @XIPH_CFLAGS@

EXTRA_DIST = thread net timing avl
EXTRA_ices_SOURCES = im_oss.c im_sun.c im_alsa.c

if HAVE_OSS
oss = im_oss.c
endif

if HAVE_SUN_AUDIO
sun = im_sun.c
endif

if HAVE_ALSA
alsa = im_alsa.c
endif

dist_noinst_HEADERS = cfgparse.h input.h inputmodule.h im_playlist.h signals.h stream.h reencode.h encode.h playlist_basic.h logging.h im_stdinpcm.h event.h stream_shared.h metadata.h audio.h resample.h im_sun.h im_oss.h im_alsa.h

ices_SOURCES = input.c cfgparse.c stream.c ices.c signals.c im_playlist.c reencode.c encode.c playlist_basic.c im_stdinpcm.c stream_shared.c metadata.c playlist_script.c audio.c resample.c $(oss) $(sun) $(alsa)

ices_LDADD = log/libicelog.la @ALSA_LIBS@

INCLUDES = -Inet -Ithread -Iavl -Ilog -Itiming

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"

