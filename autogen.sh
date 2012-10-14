#!/bin/sh
# Run this to generate all the initial makefiles, etc.

libtoolize --force --copy
intltoolize --force
gtkdocize --copy
aclocal
autoconf
autoheader
automake --add-missing --copy --foreign
./configure $@
