/* Copyright (C) 1989, 1990, 1991, 1992, 2000, 2001, 2002, 2003
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <stdarg.h>
#include <assert.h>

#ifdef HAVE_STRERROR
#include <string.h>
#else
extern char *strerror ();
#endif

#include <sys/wait.h>

#include <locale.h>
#include <libintl.h>
#define _(String) gettext (String)

#include "manconfig.h"
#include "error.h"
#include "pipeline.h"

command *command_new (const char *name)
{
	command *cmd = xmalloc (sizeof *cmd);
	char *name_copy;

	cmd->name = xstrdup (name);
	cmd->argc = 0;
	cmd->argv_max = 4;
	cmd->argv = xmalloc (cmd->argv_max * sizeof *cmd->argv);

	/* argv[0] is the basename of the command name. */
	name_copy = xstrdup (name);
	command_arg (cmd, basename (name_copy));
	free (name_copy);

	return cmd;
}

command *command_new_argv (const char *name, va_list argv)
{
	command *cmd = command_new (name);
	command_argv (cmd, argv);
	return cmd;
}

command *command_new_args (const char *name, ...)
{
	va_list argv;
	command *cmd;

	va_start (argv, name);
	cmd = command_new_argv (name, argv);
	va_end (argv);

	return cmd;
}

command *command_dup (command *cmd)
{
	command *newcmd = xmalloc (sizeof *newcmd);
	int i;

	newcmd->name = xstrdup (cmd->name);
	newcmd->argc = cmd->argc;
	newcmd->argv_max = cmd->argv_max;
	newcmd->argv = xmalloc (newcmd->argv_max * sizeof *newcmd->argv);

	for (i = 0; i < cmd->argc; ++i)
		newcmd->argv[i] = xstrdup (cmd->argv[i]);

	return newcmd;
}

void command_arg (command *cmd, const char *arg)
{
	if (cmd->argc + 1 >= cmd->argv_max) {
		cmd->argv_max *= 2;
		cmd->argv = xrealloc (cmd->argv,
				      cmd->argv_max * sizeof *cmd->argv);
	}

	cmd->argv[cmd->argc++] = xstrdup (arg);
	cmd->argv[cmd->argc] = NULL;
}

void command_argv (command *cmd, va_list argv)
{
	const char *arg = va_arg (argv, const char *);

	while (arg) {
		command_arg (cmd, arg);
		arg = va_arg (argv, const char *);
	}
}

void command_args (command *cmd, ...)
{
	va_list argv;

	va_start (argv, cmd);
	command_argv (cmd, argv);
	va_end (argv);
}

void command_free (command *cmd)
{
	int i;

	free (cmd->name);
	for (i = 0; i < cmd->argc; ++i)
		free (cmd->argv[i]);
	free (cmd->argv);
	free (cmd);
}

pipeline *pipeline_new (void)
{
	pipeline *p = xmalloc (sizeof *p);
	p->ncommands = 0;
	p->commands_max = 4;
	p->commands = xmalloc (p->commands_max * sizeof *p->commands);
	p->pids = NULL;
	p->want_in = p->want_out = 0;
	p->infd = p->outfd = -1;
	return p;
}

pipeline *pipeline_new_commandv (command *cmd1, va_list cmdv)
{
	pipeline *p = pipeline_new ();
	pipeline_command (p, cmd1);
	pipeline_commandv (p, cmdv);
	return p;
}

pipeline *pipeline_new_commands (command *cmd1, ...)
{
	va_list cmdv;
	pipeline *p;

	va_start (cmdv, cmd1);
	p = pipeline_new_commandv (cmd1, cmdv);
	va_end (cmdv);

	return p;
}

pipeline *pipeline_join (pipeline *p1, pipeline *p2)
{
	pipeline *p = xmalloc (sizeof *p);
	int i;

	assert (!p1->pids);
	assert (!p2->pids);

	p->ncommands = p1->ncommands + p2->ncommands;
	p->commands_max = p1->ncommands + p2->ncommands;
	p->commands = xmalloc (p->commands_max * sizeof *p->commands);
	p->pids = NULL;
	p->want_in = p1->want_in;
	p->want_out = p2->want_out;
	p->infd = p1->infd;
	p->outfd = p2->outfd;

	for (i = 0; i < p1->ncommands; ++i)
		p->commands[i] = command_dup (p1->commands[i]);
	for (i = 0; i < p2->ncommands; ++i)
		p->commands[p1->ncommands + i] = command_dup (p2->commands[i]);

	return p;
}

void pipeline_command (pipeline *p, command *cmd)
{
	if (p->ncommands > p->commands_max) {
		p->commands_max *= 2;
		p->commands = xrealloc (p->commands,
					p->commands_max * sizeof *p->commands);
	}

	p->commands[p->ncommands++] = cmd;
}

void pipeline_command_args (pipeline *p, const char *name, ...)
{
	va_list argv;
	command *cmd;

	va_start (argv, name);
	cmd = command_new_argv (name, argv);
	va_end (argv);
	pipeline_command (p, cmd);
}

void pipeline_commandv (pipeline *p, va_list cmdv)
{
	command *cmd = va_arg (cmdv, command *);

	while (cmd) {
		pipeline_command (p, cmd);
		cmd = va_arg (cmdv, command *);
	}
}

void pipeline_commands (pipeline *p, ...)
{
	va_list cmdv;

	va_start (cmdv, p);
	pipeline_commandv (p, cmdv);
	va_end (cmdv);
}

/* Children exit with this status if execvp fails. */
#define EXEC_FAILED_EXIT_STATUS 0xff

void pipeline_start (pipeline *p)
{
	int i;
	int last_input = -1;
	int infd[2];

	assert (!p->pids);	/* pipeline not started already */
	p->pids = xmalloc (p->ncommands * sizeof *p->pids);

	if (p->want_in) {
		if (pipe (infd) < 0)
			error (FATAL, errno, _("pipe failed"));
		last_input = infd[0];
		p->infd = infd[1];
	}

	for (i = 0; i < p->ncommands; i++) {
		int pdes[2];
		pid_t pid;

		if (i != p->ncommands - 1 || p->want_out) {
			if (pipe (pdes) < 0)
				error (FATAL, errno, _("pipe failed"));
			if (i == p->ncommands - 1)
				p->outfd = pdes[0];
		}

		pid = fork ();
		if (pid < 0)
			error (FATAL, errno, _("fork failed"));
		if (pid == 0) {
			/* child */
			if (last_input != -1) {
				if (close (0) < 0)
					error (FATAL, errno,
					       _("close failed"));
				if (dup (last_input) < 0)
					error (FATAL, errno, _("dup failed"));
				if (close (last_input) < 0)
					error (FATAL, errno,
					       _("close failed"));
			}

			if (i != p->ncommands - 1 || p->want_out) {
				if (close (1) < 0)
					error (FATAL, errno,
					       _("close failed"));
				if (dup (pdes[1]) < 0)
					error (FATAL, errno, _("dup failed"));
				if (close (pdes[1]) < 0)
					error (FATAL, errno,
					       _("close failed"));
				if (close (pdes[0]))
					error (FATAL, errno,
					       _("close failed"));
			}

			if (p->infd != -1)
				close (p->infd);

			execvp (p->commands[i]->name, p->commands[i]->argv);
			error (EXEC_FAILED_EXIT_STATUS, errno,
			       _("couldn't exec %s"), p->commands[i]->name);
		}

		/* in the parent */
		if (last_input != -1) {
			if (close (last_input) < 0)
				error (FATAL, errno, _("close failed"));
		}
		if (i != p->ncommands - 1 || p->want_out) {
			if (close (pdes[1]) < 0)
				error (FATAL, errno, _("close failed"));
			last_input = pdes[0];
		}
		p->pids[i] = pid;
	}
}

/* TODO: Perhaps it would be useful to wait on multiple pipelines
 * simultaneously? Then you could do:
 *
 *   p1->want_out = 1;
 *   p2->want_in = 1;
 *   p3->want_in = 1;
 *   pipeline_start (p1);
 *   pipeline_start (p2);
 *   pipeline_start (p3);
 *   ... select() on p1's output, p2's input, and p3's input, and glue them
 *       together ...
 *   pipeline_wait (p1, p2, p3);
 *
 * ... and have processes exit as their input is closed by other processes
 * exiting, etc.
 *
 * This function is kind of broken if multiple pipelines exist, anyway, or
 * even if there are other child processes not created by the pipeline
 * library. Unfortunately, spinning and polling non-blocking waitpid() is
 * going to suck. Perhaps ignoring the second case is OK.
 */
int pipeline_wait (pipeline *p)
{
	int ret = 0;
	int proc_count = p->ncommands;

	assert (p->pids);	/* pipeline started */

	if (p->infd != -1) {
		close (p->infd);
		p->infd = -1;
	}

	while (proc_count > 0) {
		int i;
		int status;

		pid_t pid = wait (&status);

		if (pid < 0)
			error (FATAL, errno, _("wait failed"));
		for (i = 0; i < p->ncommands; i++)
			if (p->pids[i] == pid) {
				p->pids[i] = -1;
				--proc_count;
				if (WIFSIGNALED (status)) {
					int sig = WTERMSIG (status);
#ifdef SIGPIPE
					if (sig != SIGPIPE)
#endif /* SIGPIPE */
					{
						error (0, 0, _("%s: %s%s"),
						       p->commands[i]->name,
						       xstrsignal (sig),
						       WCOREDUMP (status) ?
							 " (core dumped)" :
							 "");
						ret |= 2;
					}
				} else if (WIFEXITED (status)) {
					int exit_status = WEXITSTATUS (status);

					if (exit_status ==
					    EXEC_FAILED_EXIT_STATUS)
						ret |= 4;
					else if (exit_status != 0)
						ret |= 1;
				} else
					error (0, 0, "unexpected status %d",
					       status);
				break;
			}
	}

	if (p->outfd != -1) {
		close (p->outfd);
		p->outfd = -1;
	}

	free (p->pids);
	p->pids = NULL;

	return ret;
}

void pipeline_free (pipeline *p)
{
	int i;

	for (i = 0; i < p->ncommands; ++i)
		command_free (p->commands[i]);
	free (p->commands);
	if (p->pids)
		free (p->pids);
	free (p);
}
