#! /bin/sh
set -e

# Copyright (C) 2010 Colin Watson.
#
# This file is free software; the author gives unlimited permission to copy
# and/or distribute it, with or without modifications, as long as this
# notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if type gnulib-tool >/dev/null 2>&1; then
	gnulib-tool --update >/dev/null
	patch -s -p1 <gnulib/gets.patch
fi
export LIBTOOLIZE_OPTIONS=--quiet
autoreconf -fi "$@"
