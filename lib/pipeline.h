/* Copyright (C) 1989, 1990, 1991, 1992, 2000, 2002
 * Free Software Foundation, Inc.
 * Copyright (C) 2003 Colin Watson.
 *   Written for groff by James Clark (jjc@jclark.com)
 *   Adapted for man-db by Colin Watson.
 *
 * This file is part of man-db.
 *
 * man-db is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * man-db is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with man-db; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>

typedef struct command {
	char *name;
	int argc;
	int argv_max;		/* size of allocated array */
	char **argv;
	int nice;
} command;

typedef struct pipeline {
	int ncommands;
	int commands_max;	/* size of allocated array */
	command **commands;
	pid_t *pids;
	int *statuses;		/* -1 until command exits */

	/* To be set by the caller. If positive, these contain
	 * caller-supplied file descriptors for the input and output of the
	 * whole pipeline. If negative, pipeline_start() will create pipes
	 * and store the input writing half and the output reading half in
	 * infd and outfd as appropriate. If zero, input and output will be
	 * left as stdin and stdout.
	 */
	int want_in, want_out;

	/* See above. The caller should consider these read-only. */
	int infd, outfd;

	/* Set by pipeline_get_infile() and pipeline_get_outfile()
	 * respectively.
	 */
	FILE *infile, *outfile;
} pipeline;

/* ---------------------------------------------------------------------- */

/* Functions to build individual commands. */

/* Construct a new command. */
command *command_new (const char *name);

/* Convenience constructors wrapping command_new() and command_arg().
 * Terminate arguments with NULL.
 */
command *command_new_argv (const char *name, va_list argv);
command *command_new_args (const char *name, ...);

/* Split argstr on whitespace to construct a command and arguments,
 * honouring shell-style single-quoting, double-quoting, and backslashes,
 * but not other shell evil like wildcards, semicolons, or backquotes. This
 * is a backward-compatibility hack to support old configuration file
 * directives; please try to avoid using it in new code.
 */
command *command_new_argstr (const char *argstr);

/* Return a duplicate of a command. */
command *command_dup (command *cmd);

/* Add an argument to a command. */
void command_arg (command *cmd, const char *arg);

/* Convenience functions wrapping command_arg().
 * Terminate arguments with NULL.
 */
void command_argv (command *cmd, va_list argv);
void command_args (command *cmd, ...);

/* Split argstr on whitespace to add a list of arguments, honouring
 * shell-style single-quoting, double-quoting, and backslashes, but not
 * other shell evil like wildcards, semicolons, or backquotes. This is a
 * backward-compatibility hack to support old configuration file directives;
 * please try to avoid using it in new code.
 */
void command_argstr (command *cmd, const char *argstr);

/* Destroy a command. */
void command_free (command *cmd);

/* ---------------------------------------------------------------------- */

/* Functions to build pipelines. */

/* Construct a new pipeline. */
pipeline *pipeline_new (void);

/* Convenience constructor wrapping pipeline_new() and pipeline_add().
 * Terminate commands with NULL.
 */
pipeline *pipeline_new_commandv (command *cmd1, va_list cmdv);
pipeline *pipeline_new_commands (command *cmd1, ...);

/* Joins two pipelines, neither of which are allowed to be started. Discards
 * want_out and outfd from p1, and want_in and infd from p2.
 */
pipeline *pipeline_join (pipeline *p1, pipeline *p2);

/* Add a command to a pipeline. */
void pipeline_command (pipeline *p, command *cmd);

/* Construct a new command and add it to a pipeline in one go. */
void pipeline_command_args (pipeline *p, const char *name, ...);

/* Construct a new command from a shell-quoted string and add it to a
 * pipeline in one go. See the comment against command_new_argstr() above if
 * you're tempted to use this function.
 */
void pipeline_command_argstr (pipeline *p, const char *argstr);

/* Convenience functions wrapping pipeline_command().
 * Terminate commands with NULL.
 */
void pipeline_commandv (pipeline *p, va_list cmdv);
void pipeline_commands (pipeline *p, ...);

/* Get streams corresponding to infd and outfd respectively. The pipeline
 * must be started.
 */
FILE *pipeline_get_infile (pipeline *p);
FILE *pipeline_get_outfile (pipeline *p);

/* Dump a string representation of p to stream. */
void pipeline_dump (pipeline *p, FILE *stream);

/* Return a string representation of p. The caller should free the result. */
char *pipeline_tostring (pipeline *p);

/* Destroy a pipeline and all its commands. */
void pipeline_free (pipeline *p);

/* ---------------------------------------------------------------------- */

/* Functions to run pipelines and handle signals. */

/* Start the processes in a pipeline. Calls error(FATAL) on error. */
void pipeline_start (pipeline *p);

/* Wait for a pipeline to complete and return the exit status. */
int pipeline_wait (pipeline *p);

/* Install a SIGCHLD handler that reaps exit statuses from child processes
 * in pipelines. This should be called once per program before calling
 * pipeline_start().
 */
void pipeline_install_sigchld (void);

#endif /* PIPELINE_H */
