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

START_TEST (test_inspect_command)
{
	command *cmd;

	cmd = command_new ("foo");
	fail_unless (!strcmp (command_tostring (cmd), "foo"));
	command_free (cmd);

	cmd = command_new_args ("foo", "bar", "baz quux", NULL);
	/* TODO: not ideal representation of commands with metacharacters */
	fail_unless (!strcmp (command_tostring (cmd), "foo bar baz quux"));
	command_free (cmd);
}
END_TEST

START_TEST (test_inspect_pipeline)
{
	pipeline *p;

	p = pipeline_new ();
	pipeline_command_args (p, "foo", "bar", NULL);
	pipeline_command_args (p, "grep", "baz", "quux", NULL);
	fail_unless (pipeline_get_ncommands (p) == 2);
	command_setenv (pipeline_get_command (p, 1), "KEY", "value");
	fail_unless (!strcmp (pipeline_tostring (p),
			      "foo bar | KEY=value grep baz quux"));
	pipeline_free (p);
}
END_TEST

Suite *inspect_suite (void)
{
	Suite *s = suite_create ("Inspect");

	TEST_CASE (s, inspect, command);
	TEST_CASE (s, inspect, pipeline);

	return s;
}

MAIN (inspect)
