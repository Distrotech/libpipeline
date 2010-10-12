/*
 * Copyright (C) 2001, 2002, 2005, 2007, 2009, 2010 Colin Watson.
 *
 * This file is part of libpipeline.
 *
 * libpipeline is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * libpipeline is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libpipeline; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA.
 */

#ifndef PIPELINE_PRIVATE_H
#define PIPELINE_PRIVATE_H

/* GCC version checking borrowed from glibc. */
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#  define GNUC_PREREQ(maj,min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
#  define GNUC_PREREQ(maj,min) 0
#endif

/* Does this compiler support format string checking? */
#if GNUC_PREREQ(2,0)
#  define ATTRIBUTE_FORMAT_PRINTF(a,b) \
	__attribute__ ((__format__ (__printf__, a, b)))
#else
#  define ATTRIBUTE_FORMAT_PRINTF(a,b)
#endif

/* Does this compiler support marking variables as unused? */
#if GNUC_PREREQ(2,4)
#  define ATTRIBUTE_UNUSED __attribute__ ((__unused__))
#else
#  define ATTRIBUTE_UNUSED
#endif

/* Does this compiler support marking functions as non-returning? */
#if GNUC_PREREQ(2,5)
#  define ATTRIBUTE_NORETURN __attribute__ ((__noreturn__))
#else
#  define ATTRIBUTE_NORETURN
#endif

/* Does this compiler support unused result checking? */
#if GNUC_PREREQ(3,4)
#  define ATTRIBUTE_WARN_UNUSED_RESULT __attribute__ ((__warn_unused_result__))
#else
#  define ATTRIBUTE_WARN_UNUSED_RESULT
#endif

/* Does this compiler support sentinel checking? */
#if GNUC_PREREQ(4,0)
#  define ATTRIBUTE_SENTINEL __attribute__ ((__sentinel__))
#else
#  define ATTRIBUTE_SENTINEL
#endif

/* exit codes */
#define OK		0	/* success */
#define FAIL		1	/* usage or syntax error */
#define FATAL		2	/* operational error */

extern char *appendstr (char *, ...)
	ATTRIBUTE_SENTINEL ATTRIBUTE_WARN_UNUSED_RESULT;

extern int debug_level;
extern void debug (const char *message, ...) ATTRIBUTE_FORMAT_PRINTF(1, 2);

#endif /* PIPELINE_PRIVATE_H */
