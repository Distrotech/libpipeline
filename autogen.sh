#! /bin/sh -e

if type gnulib-tool >/dev/null 2>&1; then
	gnulib-tool --update >/dev/null
	patch -s -p0 < gnulib/lib/xmalloc.patch
fi
export LIBTOOLIZE_OPTIONS=--quiet
autoreconf -fi "$@"
