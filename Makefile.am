## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign dist-zip
ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src conf

EXTRA_DIST = README AUTHORS COPYING TODO m4

# SCCS Definitions (for BitKeeper)
GET = true

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"
