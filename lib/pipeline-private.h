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

#include "pipeline.h"

/* exit codes */
#define OK		0	/* success */
#define FAIL		1	/* usage or syntax error */
#define FATAL		2	/* operational error */

extern char *appendstr (char *, ...)
	PIPELINE_ATTR_SENTINEL PIPELINE_ATTR_WARN_UNUSED_RESULT;

extern int debug_level;
extern void debug (const char *message, ...) PIPELINE_ATTR_FORMAT_PRINTF(1, 2);

#endif /* PIPELINE_PRIVATE_H */
