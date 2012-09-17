## Process this file with automake to produce Makefile.in

## Copyright (C) 2010 Colin Watson.
##
## This file is part of libpipeline.
##
## libpipeline is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or (at
## your option) any later version.
##
## libpipeline is distributed in the hope that it will be useful, but
## WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
## General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with libpipeline; if not, write to the Free Software
## Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
## USA

SUBDIRS = gnulib/lib lib man
if RUN_TESTS
SUBDIRS += tests
endif

dist_noinst_DATA = NEWS

EXTRA_DIST = \
	.bzrignore \
	autogen.sh \
	gnulib/gets.patch \
	gnulib/m4/gnulib-cache.m4 \
	gnulib/m4/gnulib-tool.m4 \
	release.sh

# These macro files are imported by gnulib-tool, but at present not used. We
# include them in the distribution anyway for maximum consistency.
EXTRA_DIST += \
	gnulib/m4/math_h.m4

# Note that autopoint installs into the first directory here. Yes, this
# means that it installs into gnulib/m4, so we always run gnulib-tool after
# autopoint, at least for now. See
# http://lists.gnu.org/archive/html/bug-gnulib/2009-03/msg00154.html and
# thread.
ACLOCAL_AMFLAGS = -I gnulib/m4 -I m4
