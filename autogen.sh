#!/bin/sh
# Run this to generate all the initial makefiles, etc.

libtoolize --force --copy
intltoolize --force --copy
gtkdocize --copy
aclocal
autoconf
autoheader
automake --add-missing --copy --foreign

YELP=`which yelp-build`
if test -z $YELP; then
  echo "*** The tools to build the documentation are not found,"
  echo "    please intall the yelp-tools package ***"
  exit 1
fi

if [ -z "$NOCONFIGURE" ]; then
    ./configure $@
fi
