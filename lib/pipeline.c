/* Copyright (C) 1989, 1990, 1991, 1992, 2000, 2001, 2002, 2003
 * Free Software Foundation, Inc.
 * Copyright (C) 2003, 2004, 2005, 2006, 2007 Colin Watson.
 *   Written for groff by James Clark (jjc@jclark.com)
 *   Heavily adapted and extended for man-db by Colin Watson.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
/* TODO: requires POSIX 1003.1-2001 */
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <fcntl.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>

#ifndef HAVE_STRERROR
extern char *strerror ();
#endif

#include <sys/wait.h>

#ifdef HAVE_LIBGEN_H
#  include <libgen.h>
#endif /* HAVE_LIBGEN_H */

#include "gettext.h"
#include <locale.h>
#define _(String) gettext (String)

#include "manconfig.h"
#include "cleanup.h"
#include "error.h"
#include "pipeline.h"

/* ---------------------------------------------------------------------- */

/* Functions to build individual commands. */

command *command_new (const char *name)
{
	command *cmd = xmalloc (sizeof *cmd);
	struct command_process *cmdp;
	char *name_copy;

	cmd->tag = COMMAND_PROCESS;
	cmd->name = xstrdup (name);
	cmd->nice = 0;
	cmd->discard_err = 0;

	cmdp = &cmd->u.process;

	cmdp->argc = 0;
	cmdp->argv_max = 4;
	cmdp->argv = xmalloc (cmdp->argv_max * sizeof *cmdp->argv);

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

/* As suggested in the header file, this function (for command_new_argstr()
 * and command_argstr()) is really a wart. If we didn't have to worry about
 * old configuration files then it wouldn't be necessary. Worse, the
 * definition for tr in man_db.conf currently contains single-quoting, and
 * people probably took that as a licence to do similar things, so we're
 * obliged to worry about quoting as well!
 *
 * However, we can mitigate this; shell quoting alone is safe though
 * sometimes confusing, but it's other shell constructs that tend to cause
 * real security holes. Therefore, rather than punting to 'sh -c' or
 * whatever, we parse a safe subset manually. Environment variables are not
 * currently handled because of tricky word splitting issues, but in
 * principle they could be if there's demand for it.
 */
static char *argstr_get_word (const char **argstr)
{
	char *out = NULL;
	const char *litstart = *argstr;
	enum { NONE, SINGLE, DOUBLE } quotemode = NONE;

	while (**argstr) {
		char backslashed[2];

		/* If it's just a literal character, go round again. */
		if ((quotemode == NONE && !strchr (" \t'\"\\", **argstr)) ||
		    /* nothing is special in '; terminated by ' */
		    (quotemode == SINGLE && **argstr != '\'') ||
		    /* \ is special in "; terminated by " */
		    (quotemode == DOUBLE && !strchr ("\"\\", **argstr))) {
			++*argstr;
			continue;
		}

		/* Within "", \ is only special when followed by $, `, ", or
		 * \ (or <newline> in a real shell, but we don't do that).
		 */
		if (quotemode == DOUBLE && **argstr == '\\' &&
		    !strchr ("$`\"\\", *(*argstr + 1))) {
			++*argstr;
			continue;
		}

		/* Copy any accumulated literal characters. */
		if (litstart < *argstr) {
			char *tmp = xstrndup (litstart, *argstr - litstart);
			out = strappend (out, tmp, NULL);
			free (tmp);
		}

		switch (**argstr) {
			case ' ':
			case '\t':
				/* End of word; skip over extra whitespace. */
				while (*++*argstr)
					if (!strchr (" \t", **argstr))
						break;
				return out;

			case '\'':
				if (quotemode != NONE)
					quotemode = NONE;
				else
					quotemode = SINGLE;
				litstart = ++*argstr;
				break;

			case '"':
				if (quotemode != NONE)
					quotemode = NONE;
				else
					quotemode = DOUBLE;
				litstart = ++*argstr;
				break;

			case '\\':
				backslashed[0] = *++*argstr;
				if (!backslashed[0]) {
					/* Unterminated quoting; give up. */
					if (out)
						free (out);
					return NULL;
				}
				backslashed[1] = '\0';
				out = strappend (out, backslashed, NULL);
				litstart = ++*argstr;
				break;

			default:
				assert (!"unexpected state parsing argstr");
		}
	}

	if (quotemode != NONE) {
		/* Unterminated quoting; give up. */
		if (out)
			free (out);
		return NULL;
	}

	/* Copy any accumulated literal characters. */
	if (litstart < *argstr) {
		char *tmp = xstrndup (litstart, *argstr - litstart);
		out = strappend (out, tmp, NULL);
	}

	return out;
}

command *command_new_argstr (const char *argstr)
{
	command *cmd;
	char *arg;

	arg = argstr_get_word (&argstr);
	if (!arg)
		error (FATAL, 0,
		       _("badly formed configuration directive: '%s'"),
		       argstr);
	if (STREQ (arg, "exec")) {
		/* Some old configuration files have "exec command" rather
		 * than "command"; this worked fine when being evaluated by
		 * a shell, but since exec is a shell builtin it doesn't
		 * work when being executed directly. To work around this,
		 * we just drop "exec" if it appears at the start of argstr.
		 */
		arg = argstr_get_word (&argstr);
		if (!arg)
			error (FATAL, 0,
			       _("badly formed configuration directive: '%s'"),
			       argstr);
	}
	cmd = command_new (arg);
	free (arg);

	while ((arg = argstr_get_word (&argstr))) {
		command_arg (cmd, arg);
		free (arg);
	}

	return cmd;
}

command *command_new_function (const char *name,
			       command_function_type *func, void *data)
{
	command *cmd = xmalloc (sizeof *cmd);
	struct command_function *cmdf;

	cmd->tag = COMMAND_FUNCTION;
	cmd->name = xstrdup (name);
	cmd->nice = 0;

	cmdf = &cmd->u.function;

	cmdf->func = func;
	cmdf->data = data;

	return cmd;
}

command *command_dup (command *cmd)
{
	command *newcmd = xmalloc (sizeof *newcmd);
	int i;

	newcmd->tag = cmd->tag;
	newcmd->name = xstrdup (cmd->name);
	newcmd->nice = cmd->nice;
	newcmd->discard_err = cmd->discard_err;

	switch (newcmd->tag) {
		case COMMAND_PROCESS: {
			struct command_process *cmdp = &cmd->u.process;
			struct command_process *newcmdp = &newcmd->u.process;

			newcmdp->argc = cmdp->argc;
			newcmdp->argv_max = cmdp->argv_max;
			assert (newcmdp->argc < newcmdp->argv_max);
			newcmdp->argv = xmalloc
				(newcmdp->argv_max * sizeof *newcmdp->argv);

			for (i = 0; i < cmdp->argc; ++i)
				newcmdp->argv[i] = xstrdup (cmdp->argv[i]);
			newcmdp->argv[cmdp->argc] = NULL;

			break;
		}

		case COMMAND_FUNCTION: {
			struct command_function *cmdf = &cmd->u.function;
			struct command_function *newcmdf = &newcmd->u.function;

			newcmdf->func = cmdf->func;
			newcmdf->data = cmdf->data;

			break;
		}
	}

	return newcmd;
}

void command_arg (command *cmd, const char *arg)
{
	struct command_process *cmdp;

	assert (cmd->tag == COMMAND_PROCESS);
	cmdp = &cmd->u.process;

	if (cmdp->argc + 1 >= cmdp->argv_max) {
		cmdp->argv_max *= 2;
		cmdp->argv = xrealloc (cmdp->argv,
				       cmdp->argv_max * sizeof *cmdp->argv);
	}

	cmdp->argv[cmdp->argc++] = xstrdup (arg);
	assert (cmdp->argc < cmdp->argv_max);
	cmdp->argv[cmdp->argc] = NULL;
}

void command_argv (command *cmd, va_list argv)
{
	const char *arg = va_arg (argv, const char *);

	assert (cmd->tag == COMMAND_PROCESS);

	while (arg) {
		command_arg (cmd, arg);
		arg = va_arg (argv, const char *);
	}
}

void command_args (command *cmd, ...)
{
	va_list argv;

	assert (cmd->tag == COMMAND_PROCESS);

	va_start (argv, cmd);
	command_argv (cmd, argv);
	va_end (argv);
}

void command_argstr (command *cmd, const char *argstr)
{
	char *arg;

	assert (cmd->tag == COMMAND_PROCESS);

	while ((arg = argstr_get_word (&argstr))) {
		command_arg (cmd, arg);
		free (arg);
	}
}

void command_dump (command *cmd, FILE *stream)
{
	switch (cmd->tag) {
		case COMMAND_PROCESS: {
			struct command_process *cmdp = &cmd->u.process;
			int i;

			fputs (cmd->name, stream);
			for (i = 1; i < cmdp->argc; ++i) {
				/* TODO: escape_shell()? */
				putc (' ', stream);
				fputs (cmdp->argv[i], stream);
			}

			break;
		}

		case COMMAND_FUNCTION:
			fputs (cmd->name, stream);
			break;
	}
}

char *command_tostring (command *cmd)
{
	char *out = NULL;

	switch (cmd->tag) {
		case COMMAND_PROCESS: {
			struct command_process *cmdp = &cmd->u.process;
			int i;

			out = strappend (out, cmd->name, NULL);
			for (i = 1; i < cmdp->argc; ++i)
				/* TODO: escape_shell()? */
				out = strappend (out, " ", cmdp->argv[i],
						 NULL);

			break;
		}

		case COMMAND_FUNCTION:
			out = xstrdup (cmd->name);
			break;
	}

	return out;
}

void command_free (command *cmd)
{
	int i;

	if (!cmd)
		return;

	free (cmd->name);

	switch (cmd->tag) {
		case COMMAND_PROCESS: {
			struct command_process *cmdp = &cmd->u.process;

			for (i = 0; i < cmdp->argc; ++i)
				free (cmdp->argv[i]);
			free (cmdp->argv);

			break;
		}

		case COMMAND_FUNCTION:
			break;
	}

	free (cmd);
}

/* ---------------------------------------------------------------------- */

/* Functions to build pipelines. */

pipeline *pipeline_new (void)
{
	pipeline *p = xmalloc (sizeof *p);
	p->ncommands = 0;
	p->commands_max = 4;
	p->commands = xmalloc (p->commands_max * sizeof *p->commands);
	p->pids = NULL;
	p->statuses = NULL;
	p->want_in = p->want_out = 0;
	p->want_infile = p->want_outfile = NULL;
	p->infd = p->outfd = -1;
	p->infile = p->outfile = NULL;
	p->source = NULL;
	p->buffer = NULL;
	p->buflen = p->bufmax = 0;
	p->line_cache = NULL;
	p->peek_offset = 0;
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
	assert (!p1->statuses);
	assert (!p2->statuses);

	p->ncommands = p1->ncommands + p2->ncommands;
	p->commands_max = p1->ncommands + p2->ncommands;
	p->commands = xmalloc (p->commands_max * sizeof *p->commands);
	p->pids = NULL;
	p->statuses = NULL;
	p->want_in = p1->want_in;
	p->want_infile = p1->want_infile;
	p->want_out = p2->want_out;
	p->want_outfile = p2->want_outfile;
	p->infd = p1->infd;
	p->outfd = p2->outfd;
	p->infile = p1->infile;
	p->outfile = p2->outfile;

	for (i = 0; i < p1->ncommands; ++i)
		p->commands[i] = command_dup (p1->commands[i]);
	for (i = 0; i < p2->ncommands; ++i)
		p->commands[p1->ncommands + i] = command_dup (p2->commands[i]);

	return p;
}

void pipeline_connect (pipeline *source, pipeline *sink, ...)
{
	va_list argv;
	pipeline *arg;

	/* We must be in control of output from the source pipeline. If the
	 * source isn't started, we can force this.
	 */
	if (!source->pids) {
		source->want_out = -1;
		source->want_outfile = NULL;
	}
	assert (source->want_out < 0);
	assert (!source->want_outfile);

	va_start (argv, sink);
	for (arg = sink; arg; arg = va_arg (argv, pipeline *)) {
		assert (!arg->pids); /* not started */
		arg->source = source;
		arg->want_in = -1;
		arg->want_infile = NULL;
	}
	va_end (argv);
}

void pipeline_command (pipeline *p, command *cmd)
{
	if (p->ncommands >= p->commands_max) {
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

void pipeline_command_argstr (pipeline *p, const char *argstr)
{
	pipeline_command (p, command_new_argstr (argstr));
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

FILE *pipeline_get_infile (pipeline *p)
{
	assert (p->pids);	/* pipeline started */
	assert (p->statuses);
	if (p->infile)
		return p->infile;
	else if (p->infd == -1) {
		error (0, 0, _("pipeline input not open"));
		return NULL;
	} else
		return p->infile = fdopen (p->infd, "w");
}

FILE *pipeline_get_outfile (pipeline *p)
{
	assert (p->pids);	/* pipeline started */
	assert (p->statuses);
	if (p->outfile)
		return p->outfile;
	else if (p->outfd == -1) {
		error (0, 0, _("pipeline output not open"));
		return NULL;
	} else
		return p->outfile = fdopen (p->outfd, "r");
}

void pipeline_dump (pipeline *p, FILE *stream)
{
	int i;

	for (i = 0; i < p->ncommands; ++i) {
		command_dump (p->commands[i], stream);
		if (i < p->ncommands - 1)
			fputs (" | ", stream);
	}
	fprintf (stream, " [input: {%d, %s}, output: {%d, %s}]\n",
		 p->want_in, p->want_infile ? p->want_infile : "NULL",
		 p->want_out, p->want_outfile ? p->want_outfile : "NULL");
}

char *pipeline_tostring (pipeline *p)
{
	char *out = NULL;
	int i;

	for (i = 0; i < p->ncommands; ++i) {
		char *cmdout = command_tostring (p->commands[i]);
		out = strappend (out, cmdout, NULL);
		free (cmdout);
		if (i < p->ncommands - 1)
			out = strappend (out, " | ", NULL);
	}

	return out;
}

void pipeline_free (pipeline *p)
{
	int i;

	if (!p)
		return;

	for (i = 0; i < p->ncommands; ++i)
		command_free (p->commands[i]);
	free (p->commands);
	if (p->pids)
		free (p->pids);
	if (p->statuses)
		free (p->statuses);
	free (p);
}

/* ---------------------------------------------------------------------- */

/* Functions to run pipelines and handle signals. */

static pipeline **active_pipelines = NULL;
static int n_active_pipelines = 0, max_active_pipelines = 0;

static int ignored_signals = 0;
static struct sigaction osa_sigint, osa_sigquit;

/* Children exit with this status if execvp fails. */
#define EXEC_FAILED_EXIT_STATUS 0xff

void pipeline_start (pipeline *p)
{
	int i, j;
	int last_input = -1;
	int infd[2];
	sigset_t set, oset;

	assert (!p->pids);	/* pipeline not started already */
	assert (!p->statuses);

	if (debug_level) {
		debug ("Starting pipeline: ");
		pipeline_dump (p, stderr);
	}

	if (!ignored_signals++) {
		struct sigaction sa;

		/* Ignore SIGINT and SIGQUIT while subprocesses are running,
		 * just like system().
		 */
		sa.sa_handler = SIG_IGN;
		sigemptyset (&sa.sa_mask);
		sa.sa_flags = 0;
		if (xsigaction (SIGINT, &sa, &osa_sigint) < 0)
			error (FATAL, errno, "Couldn't ignore SIGINT");
		if (xsigaction (SIGQUIT, &sa, &osa_sigquit) < 0)
			error (FATAL, errno, "Couldn't ignore SIGQUIT");
	}

	/* Add to the table of active pipelines, so that signal handlers
	 * know what to do with exit statuses. Block SIGCHLD so that we can
	 * do this safely.
	 */
	sigemptyset (&set);
	sigaddset (&set, SIGCHLD);
	sigemptyset (&oset);
	while (sigprocmask (SIG_BLOCK, &set, &oset) == -1 && errno == EINTR)
		;

	/* Grow the table if necessary. */
	if (n_active_pipelines >= max_active_pipelines) {
		int filled = max_active_pipelines;
		if (max_active_pipelines)
			max_active_pipelines *= 2;
		else
			max_active_pipelines = 4;
		/* reduces to xmalloc (...) if active_pipelines == NULL */
		active_pipelines = xrealloc
			(active_pipelines,
			 max_active_pipelines * sizeof *active_pipelines);
		memset (active_pipelines + filled, 0,
			(max_active_pipelines - filled) *
				sizeof *active_pipelines);
	}

	for (i = 0; i < max_active_pipelines; ++i)
		if (!active_pipelines[i]) {
			active_pipelines[i] = p;
			break;
		}
	assert (i < max_active_pipelines);
	++n_active_pipelines;

	/* Unblock SIGCHLD. */
	while (sigprocmask (SIG_SETMASK, &oset, NULL) == -1 && errno == EINTR)
		;

	p->pids = xmalloc (p->ncommands * sizeof *p->pids);
	p->statuses = xmalloc (p->ncommands * sizeof *p->statuses);

	if (p->want_in < 0) {
		if (pipe (infd) < 0)
			error (FATAL, errno, _("pipe failed"));
		last_input = infd[0];
		p->infd = infd[1];
	} else if (p->want_in > 0)
		last_input = p->want_in;
	else if (p->want_infile) {
		last_input = open (p->want_infile, O_RDONLY);
		if (last_input < 0)
			error (FATAL, errno, _("can't open %s"),
			       p->want_infile);
	}

	for (i = 0; i < p->ncommands; i++) {
		int pdes[2];
		pid_t pid;
		int output_read = -1, output_write = -1;

		if (i != p->ncommands - 1 || p->want_out < 0) {
			if (pipe (pdes) < 0)
				error (FATAL, errno, _("pipe failed"));
			if (i == p->ncommands - 1)
				p->outfd = pdes[0];
			output_read = pdes[0];
			output_write = pdes[1];
		} else if (i == p->ncommands - 1) {
			if (p->want_out > 0)
				output_write = p->want_out;
			else if (p->want_outfile) {
				output_write = open (p->want_outfile,
						     O_WRONLY);
				if (output_write < 0)
					error (FATAL, errno, "can't open %s",
					       p->want_outfile);
			}
		}

		/* Block SIGCHLD so that the signal handler doesn't collect
		 * the exit status before we've filled in the pids array.
		 */
		sigemptyset (&set);
		sigaddset (&set, SIGCHLD);
		sigemptyset (&oset);
		while (sigprocmask (SIG_BLOCK, &set, &oset) == -1 &&
		       errno == EINTR)
			;

		pid = fork ();
		if (pid < 0)
			error (FATAL, errno, _("fork failed"));
		if (pid == 0) {
			/* child */
			pop_all_cleanups ();

			/* input, reading side */
			if (last_input != -1) {
				if (dup2 (last_input, 0) < 0)
					error (FATAL, errno, _("dup2 failed"));
				if (close (last_input) < 0)
					error (FATAL, errno,
					       _("close failed"));
			}

			/* output, writing side */
			if (output_write != -1) {
				if (dup2 (output_write, 1) < 0)
					error (FATAL, errno, _("dup2 failed"));
				if (close (output_write) < 0)
					error (FATAL, errno,
					       _("close failed"));
			}

			/* output, reading side */
			if (output_read != -1)
				if (close (output_read))
					error (FATAL, errno,
					       _("close failed"));

			/* input from first command, writing side; must close
			 * it in every child because it has to be created
			 * before forking anything
			 */
			if (p->infd != -1)
				if (close (p->infd))
					error (FATAL, errno,
					       _("close failed"));

			/* inputs and outputs from other active pipelines */
			for (j = 0; j < n_active_pipelines; ++j) {
				pipeline *active = active_pipelines[j];
				if (!active || active == p)
					continue;
				/* ignore failures */
				if (active->infd != -1)
					close (active->infd);
				if (active->outfd != -1)
					close (active->outfd);
			}

			if (p->commands[i]->nice)
				nice (p->commands[i]->nice);

			if (p->commands[i]->discard_err) {
				int devnull = open ("/dev/null", O_WRONLY);
				if (devnull != -1) {
					dup2 (devnull, 2);
					close (devnull);
				}
			}

			/* Restore signals. */
			xsigaction (SIGINT, &osa_sigint, NULL);
			xsigaction (SIGQUIT, &osa_sigquit, NULL);

			switch (p->commands[i]->tag) {
				case COMMAND_PROCESS: {
					struct command_process *cmdp =
						&p->commands[i]->u.process;
					execvp (p->commands[i]->name,
						cmdp->argv);
				}

				/* TODO: ideally, could there be a facility
				 * to execute non-blocking functions without
				 * needing to fork?
				 */
				case COMMAND_FUNCTION: {
					struct command_function *cmdf =
						&p->commands[i]->u.function;
					(*cmdf->func) (cmdf->data);
					exit (0);
				}
			}

			error (EXEC_FAILED_EXIT_STATUS, errno,
			       _("can't execute %s"), p->commands[i]->name);
		}

		/* in the parent */
		if (last_input != -1) {
			if (close (last_input) < 0)
				error (FATAL, errno, _("close failed"));
		}
		if (output_write != -1) {
			if (close (output_write) < 0)
				error (FATAL, errno, _("close failed"));
		}
		if (output_read != -1)
			last_input = output_read;
		p->pids[i] = pid;
		p->statuses[i] = -1;

		/* Unblock SIGCHLD. */
		while (sigprocmask (SIG_SETMASK, &oset, NULL) == -1 &&
		       errno == EINTR)
			;

		debug ("Started \"%s\", pid %d\n", p->commands[i]->name, pid);
	}

	if (p->ncommands == 0)
		p->outfd = last_input;
}

static int sigchld = 0;
static int queue_sigchld = 0;

static int reap_children (int block)
{
	pid_t pid;
	int status;
	int collected = 0;

	do {
		int i;

		if (sigchld) {
			/* Deal with a SIGCHLD delivery. */
			pid = waitpid (-1, &status, WNOHANG);
			--sigchld;
		} else
			pid = waitpid (-1, &status, block ? 0 : WNOHANG);

		if (pid < 0 && errno == EINTR) {
			/* Try again. */
			pid = 0;
			continue;
		}

		if (pid <= 0)
			/* We've run out of children to reap. */
			break;

		++collected;

		/* Deliver the command status if possible. */
		for (i = 0; i < n_active_pipelines; ++i) {
			pipeline *p = active_pipelines[i];
			int j;

			if (!p || !p->pids || !p->statuses)
				continue;

			for (j = 0; j < p->ncommands; ++j) {
				if (p->pids[j] == pid) {
					p->statuses[j] = status;
					i = n_active_pipelines;
					break;
				}
			}
		}
	} while ((sigchld || block == 0) && pid >= 0);

	if (collected)
		return collected;
	else
		return -1;
}

int pipeline_wait (pipeline *p)
{
	int ret = 0;
	int proc_count = p->ncommands;
	int i;
	int raise_signal = 0;

	if (debug_level) {
		debug ("Waiting for pipeline: ");
		pipeline_dump (p, stderr);
	}

	assert (p->pids);	/* pipeline started */
	assert (p->statuses);

	if (p->infile) {
		if (fclose (p->infile))
			error (0, errno,
			       _("closing pipeline input stream failed"));
		p->infile = NULL;
		p->infd = -1;
	} else if (p->infd != -1) {
		if (close (p->infd))
			error (0, errno, _("closing pipeline input failed"));
		p->infd = -1;
	}

	if (p->outfile) {
		if (fclose (p->outfile)) {
			error (0, errno,
			       _("closing pipeline output stream failed"));
			ret = 1;
		}
		p->outfile = NULL;
		p->outfd = -1;
	} else if (p->outfd != -1) {
		if (close (p->outfd)) {
			error (0, errno, _("closing pipeline output failed"));
			ret = 1;
		}
		p->outfd = -1;
	}

	/* Tell the SIGCHLD handler not to get in our way. */
	queue_sigchld = 1;

	while (proc_count > 0) {
		int r;

		debug ("Active processes (%d):\n", proc_count);

		/* Check for any statuses already collected by SIGCHLD
		 * handlers or the previous iteration before calling
		 * reap_children() again.
		 */
		for (i = 0; i < p->ncommands; ++i) {
			int status;

			if (p->pids[i] == -1)
				continue;

			debug ("  \"%s\" (%d) -> %d\n",
			       p->commands[i]->name, p->pids[i],
			       p->statuses[i]);

			if (p->statuses[i] == -1)
				continue;

			status = p->statuses[i];
			p->pids[i] = -1;
			--proc_count;
			if (WIFSIGNALED (status)) {
				int sig = WTERMSIG (status);
#ifdef SIGPIPE
				if (sig == SIGPIPE)
					status = 0;
				else {
#endif /* SIGPIPE */
					/* signals currently blocked,
					 * re-raise later
					 */
					if (sig == SIGINT || sig == SIGQUIT)
						raise_signal = sig;
					if (WCOREDUMP (status))
						error (0, 0,
						       _("%s: %s "
							 "(core dumped)"),
						       p->commands[i]->name,
						       xstrsignal (sig));
					else
						error (0, 0, _("%s: %s"),
						       p->commands[i]->name,
						       xstrsignal (sig));
#ifdef SIGPIPE
				}
#endif /* SIGPIPE */
			} else if (!WIFEXITED (status))
				error (0, 0, "unexpected status %d",
				       status);

			if (i == p->ncommands - 1)
				ret = status;
		}

		assert (proc_count >= 0);
		if (proc_count == 0)
			break;

		errno = 0;
		r = reap_children (1);

		if (r == -1 && errno == ECHILD)
			/* Eh? The pipeline was allegedly still running, so
			 * we shouldn't have got ECHILD.
			 */
			error (FATAL, errno, _("waitpid failed"));
	}

	queue_sigchld = 0;

	for (i = 0; i < n_active_pipelines; ++i)
		if (active_pipelines[i] == p)
			active_pipelines[i] = NULL;

	free (p->pids);
	p->pids = NULL;
	free (p->statuses);
	p->statuses = NULL;

	if (!--ignored_signals) {
		/* Restore signals. */
		xsigaction (SIGINT, &osa_sigint, NULL);
		xsigaction (SIGQUIT, &osa_sigquit, NULL);
	}

	if (raise_signal)
		raise (raise_signal);

	return ret;
}

static void pipeline_sigchld (int signum)
{
	assert (signum == SIGCHLD);

	++sigchld;

	if (!queue_sigchld) {
		int save_errno = errno;
		reap_children (0);
		errno = save_errno;
	}
}

void pipeline_install_sigchld (void)
{
	struct sigaction act;

	memset (&act, 0, sizeof act);
	act.sa_handler = &pipeline_sigchld;
	sigemptyset (&act.sa_mask);
	sigaddset (&act.sa_mask, SIGINT);
	sigaddset (&act.sa_mask, SIGTERM);
	sigaddset (&act.sa_mask, SIGHUP);
	sigaddset (&act.sa_mask, SIGCHLD);
	act.sa_flags = 0;
#ifdef SA_NOCLDSTOP
	act.sa_flags |= SA_NOCLDSTOP;
#endif
#ifdef SA_RESTART
	act.sa_flags |= SA_RESTART;
#endif
	if (xsigaction (SIGCHLD, &act, NULL) == -1)
		error (FATAL, errno, _("can't install SIGCHLD handler"));
}

void pipeline_pump (pipeline *p, ...)
{
	va_list argv;
	int argc, i, j;
	pipeline *arg, **pieces;
	size_t *pos;
	int *known_source, *blocking_in, *blocking_out, *waiting, *write_error;
	struct sigaction sa, osa_sigpipe;

	/* Count pipelines and allocate space for arrays. */
	va_start (argv, p);
	argc = 0;
	for (arg = p; arg; arg = va_arg (argv, pipeline *))
		++argc;
	va_end (argv);
	pieces = xmalloc (argc * sizeof *pieces);
	pos = xmalloc (argc * sizeof *pos);
	known_source = xmalloc (argc * sizeof *known_source);
	blocking_in = xmalloc (argc * sizeof *blocking_in);
	blocking_out = xmalloc (argc * sizeof *blocking_out);
	waiting = xmalloc (argc * sizeof *waiting);
	write_error = xmalloc (argc * sizeof *write_error);

	/* Set up arrays of pipelines and their read positions. Start all
	 * pipelines if necessary.
	 */
	va_start (argv, p);
	for (arg = p, i = 0; i < argc; arg = va_arg (argv, pipeline *), ++i) {
		pieces[i] = arg;
		pos[i] = 0;
		if (!pieces[i]->pids)
			pipeline_start (pieces[i]);
	}
	assert (arg == NULL);
	va_end (argv);

	/* All source pipelines must be supplied as arguments. */
	memset (known_source, 0, argc * sizeof *known_source);
	for (i = 0; i < argc; ++i) {
		int found = 0;
		if (!pieces[i]->source)
			continue;
		for (j = 0; j < argc; ++j) {
			if (pieces[i]->source == pieces[j]) {
				known_source[j] = found = 1;
				break;
			}
		}
		assert (found);
	}

	memset (blocking_in, 0, argc * sizeof *blocking_in);
	memset (blocking_out, 0, argc * sizeof *blocking_out);
	for (i = 0; i < argc; ++i) {
		int flags;
		if (pieces[i]->infd != -1) {
			flags = fcntl (pieces[i]->infd, F_GETFL);
			if (!(flags & O_NONBLOCK)) {
				blocking_in[i] = 1;
				fcntl (pieces[i]->infd, F_SETFL,
				       flags | O_NONBLOCK);
			}
		}
		if (pieces[i]->outfd != -1) {
			flags = fcntl (pieces[i]->outfd, F_GETFL);
			if (!(flags & O_NONBLOCK)) {
				blocking_out[i] = 1;
				fcntl (pieces[i]->outfd, F_SETFL,
				       flags | O_NONBLOCK);
			}
		}
	}

	memset (waiting, 0, argc * sizeof *waiting);
	memset (write_error, 0, argc * sizeof *write_error);

#ifdef SIGPIPE
	sa.sa_handler = SIG_IGN;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = 0;
	xsigaction (SIGPIPE, &sa, &osa_sigpipe);
#endif

#ifdef SA_RESTART
	/* We rely on getting EINTR from select. */
	xsigaction (SIGCHLD, NULL, &sa);
	sa.sa_flags &= ~SA_RESTART;
	xsigaction (SIGCHLD, &sa, NULL);
#endif

	for (;;) {
		fd_set rfds, wfds;
		int maxfd = -1;
		int ret;

		/* If a source dies and all data from it has been written to
		 * all sinks, close the writing end of the pipe to each of
		 * its sinks.
		 */
		for (i = 0; i < argc; ++i) {
			if (!known_source[i] || pieces[i]->outfd != -1 ||
			    pipeline_peek_size (pieces[i]))
				continue;
			for (j = 0; j < argc; ++j) {
				if (pieces[j]->source == pieces[i] &&
				    pieces[j]->infd != -1) {
					if (close (pieces[j]->infd))
						error (0, errno,
						       _("closing pipeline "
							 "input failed"));
					pieces[j]->infd = -1;
				}
			}
		}

		/* If all sinks on a source have died, close the reading end
		 * of the pipe from that source.
		 */
		for (i = 0; i < argc; ++i) {
			int got_sink = 0;
			if (!known_source[i] || pieces[i]->outfd == -1)
				continue;
			for (j = 0; j < argc; ++j) {
				if (pieces[j]->source == pieces[i] &&
				    pieces[j]->infd != -1) {
					got_sink = 1;
					break;
				}
			}
			if (got_sink)
				continue;
			if (close (pieces[i]->outfd))
				error (0, errno,
				       _("closing pipeline output failed"));
			pieces[i]->outfd = -1;
		}

		FD_ZERO (&rfds);
		FD_ZERO (&wfds);
		for (i = 0; i < argc; ++i) {
			if (pieces[i]->source && pieces[i]->infd != -1 &&
			    !waiting[i]) {
				FD_SET (pieces[i]->infd, &wfds);
				if (pieces[i]->infd > maxfd)
					maxfd = pieces[i]->infd;
			}
			if (known_source[i] && pieces[i]->outfd != -1) {
				FD_SET (pieces[i]->outfd, &rfds);
				if (pieces[i]->outfd > maxfd)
					maxfd = pieces[i]->outfd;
			}
		}
		if (maxfd == -1)
			break; /* nothing meaningful left to do */

		ret = select (maxfd + 1, &rfds, &wfds, NULL, NULL);
		if (ret < 0 && errno == EINTR) {
			/* Did a source or sink pipeline die? */
			for (i = 0; i < argc; ++i) {
				if (pieces[i]->ncommands == 0)
					continue;
				if (known_source[i] &&
				    pieces[i]->outfd != -1) {
					int last = pieces[i]->ncommands - 1;
					assert (pieces[i]->statuses);
					if (pieces[i]->statuses[last] != -1) {
						debug ("source pipeline %d "
						       "died\n", i);
						close (pieces[i]->outfd);
						pieces[i]->outfd = -1;
					}
				}
				if (pieces[i]->source &&
				    pieces[i]->infd != -1) {
					assert (pieces[i]->statuses);
					if (pieces[i]->statuses[0] != -1) {
						debug ("sink pipeline %d "
						       "died\n", i);
						close (pieces[i]->infd);
						pieces[i]->infd = -1;
					}
				}
			}
			continue;
		} else if (ret < 0)
			error (FATAL, errno, "select");

		/* Read a block of data from each available source pipeline. */
		for (i = 0; i < argc; ++i) {
			size_t peek_size, len;

			if (!known_source[i] || pieces[i]->outfd == -1)
				continue;
			if (!FD_ISSET (pieces[i]->outfd, &rfds))
				continue;

			peek_size = pipeline_peek_size (pieces[i]);
			len = peek_size + 4096;
			if (!pipeline_peek (pieces[i], &len) ||
			    len == peek_size) {
				/* Error or end-of-file; skip this pipeline
				 * from now on.
				 */
				debug ("source pipeline %d returned error "
				       "or EOF\n", i);
				close (pieces[i]->outfd);
				pieces[i]->outfd = -1;
			} else
				/* This is rather a large hammer. Whenever
				 * any data is read from any source
				 * pipeline, we go through and retry all
				 * sink pipelines, even if they aren't
				 * receiving data from the source in
				 * question. This probably results in a few
				 * more passes around the select() loop, but
				 * it eliminates some annoyingly fiddly
				 * bookkeeping.
				 */
				memset (waiting, 0, argc * sizeof *waiting);
		}

		/* Write as much data as we can to each available sink
		 * pipeline.
		 */
		for (i = 0; i < argc; ++i) {
			const char *block;
			size_t peek_size;
			ssize_t w;
			size_t minpos;

			if (!pieces[i]->source || pieces[i]->infd == -1)
				continue;
			if (!FD_ISSET (pieces[i]->infd, &wfds))
				continue;
			peek_size = pipeline_peek_size (pieces[i]->source);
			if (peek_size <= pos[i]) {
				/* Disable reading until data is read from a
				 * source fd or a child process exits, so
				 * that we neither spin nor block if the
				 * source is slow.
				 */
				waiting[i] = 1;
				continue;
			}

			/* peek a block from the source */
			block = pipeline_peek (pieces[i]->source, &peek_size);
			/* should all already be in the peek cache */
			assert (block);
			assert (peek_size);

			/* write as much of it as will fit to the sink */
			for (;;) {
				w = write (pieces[i]->infd, block + pos[i],
					   peek_size - pos[i]);
				if (w >= 0 || errno == EAGAIN)
					break;
				if (errno == EINTR)
					continue;
				/* Failure to save a cat page shouldn't
				 * impede displaying the page in a pager, so
				 * we report errors later.
				 */
				if (errno != EPIPE)
					write_error[i] = errno;
				close (pieces[i]->infd);
				pieces[i]->infd = -1;
				goto next_sink;
			}
			pos[i] += w;
			minpos = pos[i];

			/* check other sinks on the same source, and update
			 * the source's read position if earlier data is no
			 * longer needed by any sink
			 */
			for (j = 0; j < argc; ++j) {
				if (pieces[i]->source != pieces[j]->source ||
				    pieces[j]->infd == -1)
					continue;
				if (pos[j] < minpos)
					minpos = pos[j];
				/* If the source is dead and all data has
				 * been written to this sink, close the
				 * writing end of the pipe to the sink.
				 */
				if (pieces[j]->source->outfd == -1 &&
				    pos[j] >= peek_size) {
					close (pieces[j]->infd);
					pieces[j]->infd = -1;
				}
			}

			/* If some data has been written to all sinks,
			 * discard it from the source's peek cache.
			 */
			pipeline_peek_skip (pieces[i]->source, minpos);
			for (j = 0; j < argc; ++j) {
				if (pieces[i]->source == pieces[j]->source)
					pos[j] -= minpos;
			}
next_sink:		;
		}
	}

#ifdef SA_RESTART
	xsigaction (SIGCHLD, NULL, &sa);
	sa.sa_flags |= SA_RESTART;
	xsigaction (SIGCHLD, &sa, NULL);
#endif

#ifdef SIGPIPE
	xsigaction (SIGPIPE, &osa_sigpipe, NULL);
#endif

	for (i = 0; i < argc; ++i) {
		int flags;
		if (blocking_in[i] && pieces[i]->infd != -1) {
			flags = fcntl (pieces[i]->infd, F_GETFL);
			fcntl (pieces[i]->infd, F_SETFL, flags & ~O_NONBLOCK);
		}
		if (blocking_out[i] && pieces[i]->outfd != -1) {
			flags = fcntl (pieces[i]->outfd, F_GETFL);
			fcntl (pieces[i]->outfd, F_SETFL, flags & ~O_NONBLOCK);
		}
	}

	for (i = 0; i < argc; ++i) {
		if (write_error[i])
			error (FATAL, write_error[i], "write to sink %d", i);
	}

	free (write_error);
	free (waiting);
	free (blocking_out);
	free (blocking_in);
	free (pieces);
	free (pos);
}

/* ---------------------------------------------------------------------- */

/* Functions to read output from pipelines. */

static const char *get_block (pipeline *p, size_t *len, int peek)
{
	size_t readstart = 0, retstart = 0;
	size_t space = p->bufmax;
	size_t toread = *len;
	ssize_t r;

	if (p->buffer && p->peek_offset) {
		if (p->peek_offset >= toread) {
			/* We've got the whole thing in the peek cache; just
			 * return it.
			 */
			const char *buffer;
			assert (p->peek_offset <= p->buflen);
			buffer = p->buffer + p->buflen - p->peek_offset;
			if (!peek)
				p->peek_offset -= toread;
			return buffer;
		} else {
			readstart = p->buflen;
			retstart = p->buflen - p->peek_offset;
			space -= p->buflen;
			toread -= p->peek_offset;
		}
	}

	if (toread > space) {
		if (p->buffer)
			p->bufmax = readstart + toread;
		else
			p->bufmax = toread;
		p->buffer = realloc (p->buffer, p->bufmax + 1);
	}

	if (!peek)
		p->peek_offset = 0;

	assert (p->outfd != -1);
	r = read (p->outfd, p->buffer + readstart, toread);
	if (r == -1)
		return NULL;
	p->buflen = readstart + r;
	if (peek)
		p->peek_offset += r;
	*len -= (toread - r);

	return p->buffer + retstart;
}

const char *pipeline_read (pipeline *p, size_t *len)
{
	return get_block (p, len, 0);
}

const char *pipeline_peek (pipeline *p, size_t *len)
{
	return get_block (p, len, 1);
}

size_t pipeline_peek_size (pipeline *p)
{
	if (!p->buffer)
		return 0;
	return p->peek_offset;
}

void pipeline_peek_skip (pipeline *p, size_t len)
{
	if (len > 0) {
		assert (p->buffer);
		assert (len <= p->peek_offset);
		p->peek_offset -= len;
	}
}

/* readline and peekline repeatedly peek larger and larger buffers until
 * they find a newline or they fail. readline then adjusts the peek offset.
 */

static const char *get_line (pipeline *p, size_t *outlen)
{
	const size_t block = 4096;
	const char *buffer = NULL, *end = NULL;
	int i;

	if (p->line_cache) {
		free (p->line_cache);
		p->line_cache = NULL;
	}

	if (outlen)
		*outlen = 0;

	for (i = 0; ; ++i) {
		size_t plen = block * (i + 1);

		buffer = get_block (p, &plen, 1);
		if (!buffer || plen == 0)
			return NULL;

		end = strchr (buffer + block * i, '\n');
		if (!end && plen < block * (i + 1))
			/* end of file, no newline found */
			end = buffer + plen - 1;
		if (end)
			break;
	}

	if (end) {
		p->line_cache = xstrndup (buffer, end - buffer + 1);
		if (outlen)
			*outlen = end - buffer + 1;
		return p->line_cache;
	} else
		return NULL;
}

const char *pipeline_readline (pipeline *p)
{
	size_t buflen;
	const char *buffer = get_line (p, &buflen);
	if (buffer)
		p->peek_offset -= buflen;
	return buffer;
}

const char *pipeline_peekline (pipeline *p)
{
	return get_line (p, NULL);
}
