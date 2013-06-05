/*
 * Copyright (C) 2013 Peter Schiffer.
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

/*
 * Unit test for bug: https://bugzilla.redhat.com/show_bug.cgi?id=876108
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <errno.h>
#include "xvasprintf.h"

#include "common.h"

const char *program_name = "reading_long_line";

/* Must be 8194 or bigger */
#define RANDOM_STR_LEN 9000

START_TEST (test_reading_longline)
{
	/* Generate long random string */
	static const char *alphanum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz";
	char random_string[RANDOM_STR_LEN] = "";
	unsigned i;

	for (i = 0; i < RANDOM_STR_LEN - 2; i++) {
		random_string[i] = alphanum[rand () % (strlen (alphanum) - 1)];
	}
	random_string[RANDOM_STR_LEN - 1] = '\0';

	/* Write the random string to file */
	char *testfile = xasprintf ("%s/test", temp_dir);
	FILE *tfd = fopen (testfile, "w");
	if (!tfd) {
		fail ("fopen failed: %s", strerror (errno));
		return;
	}
	fprintf (tfd, "%s\n", random_string);
	fclose (tfd);

	char *expected_output = xasprintf ("%s\n", random_string);

	pipeline *p;
	const char *line;
	char *read_result = NULL, *temp = NULL;

	/* File must be read twice to reproduce the bug */
	p = pipeline_new ();
	pipeline_want_infile (p, testfile);
	pipeline_want_out (p, -1);
	pipeline_start (p);
	while ((line = pipeline_readline (p)) != NULL){
		if (read_result) {
			temp = read_result;
			read_result = xasprintf ("%s%s", read_result, line);
			free (temp);
		} else {
			read_result = xasprintf ("%s", line);
		}
	}
	pipeline_free (p);
	fail_unless (!strcmp (read_result, expected_output),
		"Returned string doesn't match the input.");

	free (read_result);
	read_result = NULL;

	p = pipeline_new ();
	pipeline_want_infile (p, testfile);
	pipeline_want_out (p, -1);
	pipeline_start (p);
	while ((line = pipeline_readline (p)) != NULL){
		if (read_result) {
			temp = read_result;
			read_result = xasprintf ("%s%s", read_result, line);
			free (temp);
		} else {
			read_result = xasprintf ("%s", line);
		}
	}
	pipeline_free (p);
	fail_unless (!strcmp (read_result, expected_output),
		"Returned string doesn't match the input.");

	free (testfile);
	free (expected_output);
	free (read_result);
}
END_TEST

Suite *reading_long_line_suite (void)
{
	Suite *s = suite_create ("Reading long line");

	TEST_CASE_WITH_FIXTURE (s, reading, longline,
		temp_dir_setup, temp_dir_teardown);

	return s;
}

MAIN (reading_long_line)
