## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

EXTRA_DIST = ices-live.xml ices-playlist.xml

debug:
        $(MAKE) all CFLAGS="@DEBUG@"

profile:
        $(MAKE) all CFLAGS="@PROFILE@"

