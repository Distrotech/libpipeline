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

#include <stdarg.h>
#include <sys/types.h>

typedef struct command {
	char *name;
	int argc;
	int argv_max;		/* size of allocated array */
	char **argv;
} command;

typedef struct pipeline {
	int ncommands;
	int commands_max;	/* size of allocated array */
	command **commands;
	pid_t *pids;
} pipeline;

/* Construct a new command. */
command *command_new (const char *name);

/* Convenience constructors wrapping command_new() and command_arg().
 * Terminate arguments with NULL.
 */
command *command_new_argv (const char *name, va_list argv);
command *command_new_args (const char *name, ...);

/* Add an argument to a command. */
void command_arg (command *cmd, const char *arg);

/* Convenience functions wrapping command_arg().
 * Terminate arguments with NULL.
 */
void command_argv (command *cmd, va_list argv);
void command_args (command *cmd, ...);

/* Destroy a command. */
void command_free (command *cmd);

/* Construct a new pipeline. */
pipeline *pipeline_new (void);

/* Convenience constructor wrapping pipeline_new() and pipeline_add().
 * Terminate commands with NULL.
 */
pipeline *pipeline_new_commandv (command *cmd1, va_list cmdv);
pipeline *pipeline_new_commands (command *cmd1, ...);

/* Add a command to a pipeline. */
void pipeline_command (pipeline *p, command *cmd);

/* Construct a new command and add it to a pipeline in one go. */
void pipeline_command_args (pipeline *p, const char *name, ...);

/* Convenience functions wrapping pipeline_command().
 * Terminate commands with NULL.
 */
void pipeline_commandv (pipeline *p, va_list cmdv);
void pipeline_commands (pipeline *p, ...);

/* Start the processes in a pipeline. Calls error(FATAL) on error. */
void pipeline_start (pipeline *p);

/* Wait for a pipeline to complete and return the exit status. */
int pipeline_wait (pipeline *p);

/* Destroy a pipeline and all its commands. */
void pipeline_free (pipeline *p);

#endif /* PIPELINE_H */
