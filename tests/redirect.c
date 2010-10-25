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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <check.h>

#include "xalloc.h"

#include "pipeline.h"

START_TEST (test_redirect_files)
{
	char *template = xstrdup ("testtmp.XXXXXX");
	int fd;
	FILE *fh;
	pipeline *p;
	const char *line;

	fd = mkstemp (template);
	if (fd < 0) {
		fail ("mkstemp failed: %s", strerror (errno));
		return;
	}
	fh = fdopen (fd, "w");
	fprintf (fh, "test data\n");
	fflush (fh);

	p = pipeline_new_command_args ("sed", "-e", "s/$/ out/", NULL);
	pipeline_want_infile (p, template);
	pipeline_want_out (p, -1);
	pipeline_start (p);
	line = pipeline_readline (p);
	fail_unless (!strcmp (line, "test data out\n"));

	fclose (fh);
	unlink (template);
}
END_TEST

Suite *redirect_suite (void)
{
	Suite *s = suite_create ("Redirect");
	TCase *t;

	t = tcase_create ("files");
	tcase_add_test (t, test_redirect_files);
	suite_add_tcase (s, t);

	return s;
}

int main (int argc PIPELINE_ATTR_UNUSED, char **argv PIPELINE_ATTR_UNUSED)
{
	int failed;
	Suite *s = redirect_suite ();
	SRunner *sr = srunner_create (s);

	srunner_run_all (sr, CK_ENV);
	failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
