/* Copyright (C) 1989, 1990, 1991, 1992, 2000, 2002
 * Free Software Foundation, Inc.
 * Copyright (C) 2003, 2004, 2005, 2007, 2008 Colin Watson.
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
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>

enum command_tag {
	COMMAND_PROCESS,
	COMMAND_FUNCTION,
	COMMAND_SEQUENCE
};

typedef void command_function_type (void *);
typedef void command_function_free_type (void *);

struct command_env {
	char *name;
	char *value;
};

typedef struct command {
	enum command_tag tag;
	char *name;
	int nice;
	int discard_err;	/* discard stderr? */
	int nenv;
	int env_max;		/* size of allocated array */
	struct command_env *env;
	union {
		struct command_process {
			int argc;
			int argv_max;	/* size of allocated array */
			char **argv;
		} process;
		struct command_function {
			command_function_type *func;
			command_function_free_type *free_func;
			void *data;
		} function;
		struct command_sequence {
			int ncommands;
			int commands_max;
			struct command **commands;
		} sequence;
	} u;
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
	 * left as stdin and stdout unless want_infile or want_outfile
	 * respectively is set.
	 */
	int want_in, want_out;

	/* To be set (and freed) by the caller. If non-NULL, these contain
	 * files to open and use as the input and output of the whole
	 * pipeline. These are only used if want_in or want_out respectively
	 * is zero. The value of using these rather than simply opening the
	 * files before starting the pipeline is that the files will be
	 * opened with the same privileges under which the pipeline is being
	 * run.
	 */
	const char *want_infile, *want_outfile;

	/* See above. Default to -1. The caller should consider these
	 * read-only.
	 */
	int infd, outfd;

	/* Set by pipeline_get_infile() and pipeline_get_outfile()
	 * respectively. Default to NULL.
	 */
	FILE *infile, *outfile;

	/* Set by pipeline_connect() to record that this pipeline reads its
	 * input from another pipeline. Defaults to NULL.
	 */
	struct pipeline *source;

	/* Private buffer for use by read/peek functions. */
	char *buffer;
	size_t buflen, bufmax;

	/* The last line returned by readline/peekline. Private. */
	char *line_cache;

	/* The amount of data at the end of buffer which has been
	 * read-ahead, either by an explicit peek or by readline/peekline
	 * reading a block at a time to save work. Private.
	 */
	size_t peek_offset;

	/* If set, ignore SIGINT and SIGQUIT while the pipeline is running,
	 * like system(). Defaults to 1.
	 */
	int ignore_signals;
} pipeline;

/* ---------------------------------------------------------------------- */

/* Functions to build individual commands. */

/* Construct a new command. */
command *command_new (const char *name);

/* Convenience constructors wrapping command_new() and command_arg().
 * Terminate arguments with NULL.
 */
command *command_new_argv (const char *name, va_list argv);
command *command_new_args (const char *name, ...) ATTRIBUTE_SENTINEL;

/* Split argstr on whitespace to construct a command and arguments,
 * honouring shell-style single-quoting, double-quoting, and backslashes,
 * but not other shell evil like wildcards, semicolons, or backquotes. This
 * is a backward-compatibility hack to support old configuration file
 * directives; please try to avoid using it in new code.
 */
command *command_new_argstr (const char *argstr);

/* Construct a new command that calls a given function rather than executing
 * a process. The data argument is passed as the function's only argument,
 * and will be freed before returning using free_func (if non-NULL).
 *
 * command_* functions that deal with arguments cannot be used with the
 * command returned by this function.
 */
command *command_new_function (const char *name,
			       command_function_type *func,
			       command_function_free_type *free_func,
			       void *data);

/* Construct a new command that runs a sequence of commands. The commands
 * will be executed in forked children; if any exits non-zero then it will
 * terminate the sequence, as with "&&" in shell.
 *
 * command_* functions that deal with arguments cannot be used with the
 * command returned by this function.
 */
command *command_new_sequence (const char *name, ...) ATTRIBUTE_SENTINEL;

/* Return a duplicate of a command. */
command *command_dup (command *cmd);

/* Add an argument to a command. */
void command_arg (command *cmd, const char *arg);

/* Convenience functions wrapping command_arg().
 * Terminate arguments with NULL.
 */
void command_argv (command *cmd, va_list argv);
void command_args (command *cmd, ...) ATTRIBUTE_SENTINEL;

/* Split argstr on whitespace to add a list of arguments, honouring
 * shell-style single-quoting, double-quoting, and backslashes, but not
 * other shell evil like wildcards, semicolons, or backquotes. This is a
 * backward-compatibility hack to support old configuration file directives;
 * please try to avoid using it in new code.
 */
void command_argstr (command *cmd, const char *argstr);

/* Set an environment variable while running this command. */
void command_setenv (command *cmd, const char *name, const char *value);

/* Add a command to a sequence. */
void command_sequence_command (command *cmd, command *child);

/* Dump a string representation of a command to stream. */
void command_dump (command *cmd, FILE *stream);

/* Return a string representation of a command. The caller should free the
 * result.
 */
char *command_tostring (command *cmd);

/* Destroy a command. Safely does nothing on NULL. */
void command_free (command *cmd);

/* ---------------------------------------------------------------------- */

/* Functions to build pipelines. */

/* Construct a new pipeline. */
pipeline *pipeline_new (void);

/* Convenience constructor wrapping pipeline_new() and pipeline_command().
 * Terminate commands with NULL.
 */
pipeline *pipeline_new_commandv (command *cmd1, va_list cmdv);
pipeline *pipeline_new_commands (command *cmd1, ...) ATTRIBUTE_SENTINEL;

/* Joins two pipelines, neither of which are allowed to be started. Discards
 * want_out, want_outfile, and outfd from p1, and want_in, want_infile, and
 * infd from p2.
 */
pipeline *pipeline_join (pipeline *p1, pipeline *p2);

/* Connect the input of one or more sink pipelines to the output of a source
 * pipeline. The source pipeline may be started, but in that case want_out
 * must be negative; otherwise, discards want_out from source. In any event,
 * discards want_in from all sinks, none of which are allowed to be started.
 * Terminate arguments with NULL.
 *
 * This is an application-level connection; data may be intercepted between
 * the pipelines by the program before calling pipeline_pump(), which sets
 * data flowing from the source to the sinks. It is primarily useful when
 * more than one sink pipeline is involved, in which case the pipelines
 * cannot simply be concatenated into one.
 */
void pipeline_connect (pipeline *source, pipeline *sink, ...)
	ATTRIBUTE_SENTINEL;

/* Add a command to a pipeline. */
void pipeline_command (pipeline *p, command *cmd);

/* Construct a new command and add it to a pipeline in one go. */
void pipeline_command_args (pipeline *p, const char *name, ...)
	ATTRIBUTE_SENTINEL;

/* Construct a new command from a shell-quoted string and add it to a
 * pipeline in one go. See the comment against command_new_argstr() above if
 * you're tempted to use this function.
 */
void pipeline_command_argstr (pipeline *p, const char *argstr);

/* Convenience functions wrapping pipeline_command().
 * Terminate commands with NULL.
 */
void pipeline_commandv (pipeline *p, va_list cmdv);
void pipeline_commands (pipeline *p, ...) ATTRIBUTE_SENTINEL;

/* Get streams corresponding to infd and outfd respectively. The pipeline
 * must be started.
 */
FILE *pipeline_get_infile (pipeline *p);
FILE *pipeline_get_outfile (pipeline *p);

/* Dump a string representation of p to stream. */
void pipeline_dump (pipeline *p, FILE *stream);

/* Return a string representation of p. The caller should free the result. */
char *pipeline_tostring (pipeline *p);

/* Destroy a pipeline and all its commands. Safely does nothing on NULL.
 * May wait for the pipeline to complete if it has not already done so.
 */
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

/* Pump data among one or more pipelines connected using pipeline_connect()
 * until all source pipelines have reached end-of-file and all data has been
 * written to all sinks (or failed). All relevant pipelines must be
 * supplied: that is, no pipeline that has been connected to a source
 * pipeline may be supplied unless that source pipeline is also supplied.
 * Automatically starts all pipelines if they are not already started, but
 * does not wait for them.
 */
void pipeline_pump (pipeline *p, ...) ATTRIBUTE_SENTINEL;

/* ---------------------------------------------------------------------- */

/* Functions to read output from pipelines. */

/* Read len bytes of data from the pipeline, returning the data block. len
 * is updated with the number of bytes read.
 */
const char *pipeline_read (pipeline *p, size_t *len);

/* Look ahead in the pipeline's output for len bytes of data, returning the
 * data block. len is updated with the number of bytes read. The starting
 * position of the next read or peek is not affected by this call.
 */
const char *pipeline_peek (pipeline *p, size_t *len);

/* Return the number of bytes of data that can be read using pipeline_read
 * or pipeline_peek solely from the peek cache, without having to read from
 * the pipeline itself (and thus potentially block).
 */
size_t pipeline_peek_size (pipeline *p);

/* Skip over and discard len bytes of data from the peek cache. Asserts that
 * enough data is available to skip, so you may want to check using
 * pipeline_peek_size first.
 */
void pipeline_peek_skip (pipeline *p, size_t len);

/* Read a line of data from the pipeline, returning it. */
const char *pipeline_readline (pipeline *p);

/* Look ahead in the pipeline's output for a line of data, returning it. The
 * starting position of the next read or peek is not affected by this call.
 */
const char *pipeline_peekline (pipeline *p);

#endif /* PIPELINE_H */
