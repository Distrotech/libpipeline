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

TESTS = \
	basic
check_PROGRAMS = $(TESTS)

LIBPIPELINE = $(top_builddir)/lib/libpipeline.la

AM_CPPFLAGS = -I$(top_srcdir)/lib
AM_CFLAGS = @CHECK_CFLAGS@

basic_SOURCES = basic.c
basic_LDADD = $(LIBPIPELINE) @CHECK_LIBS@
