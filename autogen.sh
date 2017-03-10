#!/bin/sh
# Run this to set up the build system: configure, makefiles, etc.
set -e

srcdir=$(dirname "$0")
test -n "$srcdir" && cd "$srcdir"

(autoreconf --version) > /dev/null 2>&1 || {
        echo "You must have autoreconf installed to compile IceS."
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
        exit 1
}

echo "Updating build configuration files for IceS, please wait..."

autoreconf -isf

echo
echo "Done!"
echo "Now type '$srcdir/configure' to configure the build"
echo "Followed by 'make'"
