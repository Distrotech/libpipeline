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
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "common.h"

START_TEST (test_inspect_command)
{
	pipecmd *cmd;
	char *str;

	cmd = pipecmd_new ("foo");
	str = pipecmd_tostring (cmd);
	fail_unless (!strcmp (str, "foo"));
	free (str);
	pipecmd_free (cmd);

	cmd = pipecmd_new_args ("foo", "bar", "baz quux", NULL);
	str = pipecmd_tostring (cmd);
	/* TODO: not ideal representation of commands with metacharacters */
	fail_unless (!strcmp (str, "foo bar baz quux"));
	free (str);
	pipecmd_free (cmd);
}
END_TEST

START_TEST (test_inspect_pipeline)
{
	pipeline *p;
	char *str;

	p = pipeline_new ();
	pipeline_command_args (p, "foo", "bar", NULL);
	pipeline_command_args (p, "grep", "baz", "quux", NULL);
	fail_unless (pipeline_get_ncommands (p) == 2);
	pipecmd_setenv (pipeline_get_command (p, 1), "KEY", "value");
	str = pipeline_tostring (p);
	fail_unless (!strcmp (str, "foo bar | KEY=value grep baz quux"));
	free (str);
	pipeline_free (p);
}
END_TEST

static void pid_helper (void *data PIPELINE_ATTR_UNUSED)
{
	printf ("%ld\n", (long) getpid ());
	fflush (stdout);
	pause ();
}

START_TEST (test_inspect_pid)
{
	pipeline *p;
	pipecmd *cmd;
	const char *line;
	pid_t pid;

	p = pipeline_new ();
	cmd = pipecmd_new_function ("pid_helper", pid_helper, NULL, NULL);
	pipeline_command (p, cmd);
	pipeline_want_out (p, -1);
	pipeline_start (p);
	line = pipeline_readline (p);
	pid = (pid_t) atol (line);
	fail_unless (pid == pipeline_get_pid (p, 0), "pids match");
	/* Note that this test may hang if pipeline_get_pid does not work.
	 * We might be able to fix this by calling setsid at the start of
	 * the test and then killing the process group, but I'm not sure if
	 * that's sufficiently portable.
	 */
	if (pid == pipeline_get_pid (p, 0)) {
		FILE *saved_stderr;
		int status;

		/* Suppress "Terminated" errors.  We should probably have a
		 * cleaner way to do this.
		 */
		saved_stderr = stderr;
		stderr = fopen ("/dev/null", "w");
		kill (pid, SIGTERM);
		status = pipeline_wait (p);
		fclose (stderr);
		stderr = saved_stderr;

		fail_unless (status == 128 + SIGTERM,
			     "pid_helper did not indicate SIGTERM");
	}
	pipeline_free (p);
}
END_TEST

Suite *inspect_suite (void)
{
	Suite *s = suite_create ("Inspect");

	TEST_CASE (s, inspect, command);
	TEST_CASE (s, inspect, pipeline);
	TEST_CASE (s, inspect, pid);

	return s;
}

MAIN (inspect)
