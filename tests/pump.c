/*
 * Copyright (C) 2012 Colin Watson.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>

#include "full-write.h"
#include "xalloc.h"
#include "xvasprintf.h"

#include "common.h"

/* Include private definitions so that we can inspect redirections. */
#include "pipeline-private.h"

static void fail_unless_files_equal (const char *left, const char *right)
{
	pipeline *diff = pipeline_new_command_args
		("diff", "-u", left, right, NULL);
	int ret = pipeline_run (diff);
	fail_unless (ret == 0);
}

START_TEST (test_pump_connect_attaches_correctly)
{
	pipeline *one = pipeline_new ();
	pipeline *two = pipeline_new ();
	pipeline *three = pipeline_new ();

	pipeline_connect (one, two, three, NULL);
	fail_unless (one->redirect_out == REDIRECT_FD);
	fail_unless (one->want_out < 0);
	fail_unless (one->want_outfile == NULL);
	fail_unless (two->source == one);
	fail_unless (two->redirect_in == REDIRECT_FD);
	fail_unless (two->want_in < 0);
	fail_unless (two->want_infile == NULL);
	fail_unless (three->source == one);
	fail_unless (three->redirect_in == REDIRECT_FD);
	fail_unless (three->want_in < 0);
	fail_unless (three->want_infile == NULL);

	pipeline_free (three);
	pipeline_free (two);
	pipeline_free (one);
}
END_TEST

static void tee_source (void *data PIPELINE_ATTR_UNUSED)
{
	unsigned char buf[256];
	int i;

	for (i = 0; i < 256; ++i)
		buf[i] = (unsigned char) i;

	for (i = 0; i < 4096; ++i)
		full_write (fileno (stdout), buf, 256);
}

START_TEST (test_pump_tee)
{
	pipeline *source, *sink_process, *sink_function;
	char *process_outfile, *function_outfile;

	source = pipeline_new ();
	pipeline_command (source,
			  pipecmd_new_function ("source", tee_source, 
						NULL, NULL));
	sink_process = pipeline_new_command_args ("cat", NULL);
	process_outfile = xasprintf ("%s/process", temp_dir);
	pipeline_want_outfile (sink_process, process_outfile);
	sink_function = pipeline_new ();
	pipeline_command (sink_function, pipecmd_new_passthrough ());
	function_outfile = xasprintf ("%s/function", temp_dir);
	pipeline_want_outfile (sink_function, function_outfile);
	pipeline_connect (source, sink_process, sink_function, NULL);
	pipeline_pump (source, sink_process, sink_function, NULL);
	fail_unless_files_equal (process_outfile, function_outfile);

	free (function_outfile);
	free (process_outfile);
	pipeline_free (sink_function);
	pipeline_free (sink_process);
	pipeline_free (source);
}
END_TEST

Suite *pump_suite (void)
{
	Suite *s = suite_create ("Pump");

	TEST_CASE (s, pump, connect_attaches_correctly);
	TEST_CASE_WITH_FIXTURE (s, pump, tee,
				temp_dir_setup, temp_dir_teardown);

	return s;
}

MAIN (pump)
