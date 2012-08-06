## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign 1.6 dist-bzip2
ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src conf doc

EXTRA_DIST = README AUTHORS COPYING TODO m4/shout.m4 m4/vorbis.m4 m4/ogg.m4 m4/xiph_compiler.m4 m4/xiph_xml2.m4

# SCCS Definitions (for BitKeeper)
GET = true

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"
