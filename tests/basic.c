/*
 * Copyright (C) 2010 Colin Watson.
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

#include <string.h>

#include "common.h"

START_TEST (test_basic_status)
{
	pipeline *p;

	p = pipeline_new_command_args ("true", NULL);
	fail_unless (pipeline_run (p) == 0, "true did not return 0");
	p = pipeline_new_command_args ("false", NULL);
	fail_if (pipeline_run (p) == 0, "false returned 0");
}
END_TEST

START_TEST (test_basic_args)
{
	pipeline *p;
	const char *line;

	p = pipeline_new_command_args ("echo", "foo", NULL);
	pipeline_want_out (p, -1);
	pipeline_start (p);
	line = pipeline_readline (p);
	fail_unless (!strcmp (line, "foo\n"),
		     "Incorrect output from 'echo foo': '%s'", line);
	fail_unless (pipeline_wait (p) == 0, "'echo foo' did not return 0");
	pipeline_free (p);

	p = pipeline_new_command_args ("echo", "foo", "bar", NULL);
	pipeline_want_out (p, -1);
	pipeline_start (p);
	line = pipeline_readline (p);
	fail_unless (!strcmp (line, "foo bar\n"),
		     "Incorrect output from 'echo foo bar': '%s'", line);
	fail_unless (pipeline_wait (p) == 0,
		     "'echo foo bar' did not return 0");
	pipeline_free (p);
}
END_TEST

START_TEST (test_basic_pipeline)
{
	pipeline *p;
	const char *line;

	p = pipeline_new ();
	pipeline_command_args (p, "echo", "foo", NULL);
	pipeline_command_args (p, "sed", "-e", "s/foo/bar/", NULL);
	pipeline_want_out (p, -1);
	pipeline_start (p);
	line = pipeline_readline (p);
	fail_unless (!strcmp (line, "bar\n"),
		     "Incorrect output from 'echo foo | sed -e s/foo/bar/': "
		     "'%s'", line);
	fail_unless (pipeline_wait (p) == 0,
		     "'echo foo | sed -e 's/foo/bar/' did not return 0");
	pipeline_free (p);
}
END_TEST

START_TEST (test_basic_setenv)
{
	pipeline *p;

	p = pipeline_new_command_args ("sh", "-c", "exit $TEST1", NULL);
	pipecmd_setenv (pipeline_get_command (p, 0), "TEST1", "10");
	fail_unless (pipeline_run (p) == 10, "TEST1 not set properly");
}
END_TEST

START_TEST (test_basic_unsetenv)
{
	pipeline *p;
	const char *line;

	setenv ("TEST2", "foo", 1);

	p = pipeline_new_command_args ("sh", "-c", "echo $TEST2", NULL);
	pipeline_want_out (p, -1);
	pipeline_start (p);
	line = pipeline_readline (p);
	fail_unless (!strcmp (line, "foo\n"),
		     "control returned '%s', expected 'foo\n'", line);
	pipeline_wait (p);
	pipeline_free (p);

	p = pipeline_new_command_args ("sh", "-c", "echo $TEST2", NULL);
	pipecmd_unsetenv (pipeline_get_command (p, 0), "TEST2");
	pipeline_want_out (p, -1);
	pipeline_start (p);
	line = pipeline_readline (p);
	fail_unless (!strcmp (line, "\n"),
		     "unsetenv failed: returned '%s', expected '\n'", line);
	pipeline_wait (p);
	pipeline_free (p);
}
END_TEST

Suite *basic_suite (void)
{
	Suite *s = suite_create ("Basic");

	TEST_CASE (s, basic, status);
	TEST_CASE (s, basic, args);
	TEST_CASE (s, basic, pipeline);
	TEST_CASE (s, basic, setenv);
	TEST_CASE (s, basic, unsetenv);

	return s;
}

MAIN (basic)
