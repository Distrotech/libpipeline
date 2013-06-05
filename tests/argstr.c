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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include "common.h"

/* Include private definitions so that we can inspect arguments. */
#include "pipeline-private.h"

const char *program_name = "argstr";

START_TEST (test_argstr_trivial)
{
	pipecmd *cmd;

	cmd = pipecmd_new_argstr ("/bin/simple");
	fail_unless (!strcmp (cmd->name, "/bin/simple"));
	fail_unless (cmd->u.process.argc == 1);
	fail_unless (!strcmp (cmd->u.process.argv[0], "simple"));
	pipecmd_free (cmd);
}
END_TEST

START_TEST (test_argstr_torture)
{
	pipecmd *cmd;

	cmd = pipecmd_new_argstr
		("x' \\' \\$\\'\\\"\" \\'\\$\\\"\\\\ \" \\\"");
	fail_unless (!strcmp (cmd->name, "x \\"));
	fail_unless (cmd->u.process.argc == 3);
	fail_unless (!strcmp (cmd->u.process.argv[0], "x \\"));
	fail_unless (!strcmp (cmd->u.process.argv[1], "$'\" \\'$\"\\ "));
	fail_unless (!strcmp (cmd->u.process.argv[2], "\""));
	pipecmd_free (cmd);
}
END_TEST

START_TEST (test_argstr_exec)
{
	pipecmd *cmd;

	cmd = pipecmd_new_argstr ("exec /bin/foo bar");
	fail_unless (!strcmp (cmd->name, "/bin/foo"));
	fail_unless (cmd->u.process.argc == 2);
	fail_unless (!strcmp (cmd->u.process.argv[0], "foo"));
	fail_unless (!strcmp (cmd->u.process.argv[1], "bar"));
	pipecmd_free (cmd);
}
END_TEST

Suite *argstr_suite (void)
{
	Suite *s = suite_create ("Argstr");

	TEST_CASE (s, argstr, trivial);
	TEST_CASE (s, argstr, torture);
	TEST_CASE (s, argstr, exec);

	return s;
}

MAIN (argstr)
