/* Copyright (C) 1989, 1990, 1991, 1992, 2000, 2001, 2002, 2003
 * Free Software Foundation, Inc.
 * Copyright (C) 2003 Colin Watson.
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

#ifdef HAVE_LIBGEN_H
#  include <libgen.h>
#endif /* HAVE_LIBGEN_H */

#include <locale.h>
#include <libintl.h>
#define _(String) gettext (String)

#include "manconfig.h"
#include "cleanup.h"
#include "error.h"
#include "pipeline.h"

extern int debug;

/* ---------------------------------------------------------------------- */

/* Functions to build individual commands. */

command *command_new (const char *name)
{
	command *cmd = xmalloc (sizeof *cmd);
	char *name_copy;

	cmd->name = xstrdup (name);
	cmd->argc = 0;
	cmd->argv_max = 4;
	cmd->argv = xmalloc (cmd->argv_max * sizeof *cmd->argv);
	cmd->nice = 0;

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
	int quotemode = 0;

	while (**argstr) {
		char backslashed[2];

		/* If it's just a literal character, go round again. */
		if ((quotemode == 0 && !strchr (" \t'\"\\", **argstr)) ||
		    /* nothing is special in '; terminated by ' */
		    (quotemode == 1 && **argstr != '\'') ||
		    /* \ is special in "; terminated by " */
		    (quotemode == 2 && !strchr ("\"\\", **argstr))) {
			++*argstr;
			continue;
		}

		/* Within "", \ is only special when followed by $, `, ", or
		 * \ (or <newline> in a real shell, but we don't do that).
		 */
		if (quotemode == 2 && **argstr == '\\' &&
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
				if (quotemode)
					quotemode = 0;
				else
					quotemode = 1;
				litstart = ++*argstr;
				break;

			case '"':
				if (quotemode)
					quotemode = 0;
				else
					quotemode = 2;
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

	if (quotemode) {
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
	cmd = command_new (arg);
	free (arg);

	while ((arg = argstr_get_word (&argstr))) {
		command_arg (cmd, arg);
		free (arg);
	}

	return cmd;
}

command *command_dup (command *cmd)
{
	command *newcmd = xmalloc (sizeof *newcmd);
	int i;

	newcmd->name = xstrdup (cmd->name);
	newcmd->argc = cmd->argc;
	newcmd->argv_max = cmd->argv_max;
	assert (newcmd->argc < newcmd->argv_max);
	newcmd->argv = xmalloc (newcmd->argv_max * sizeof *newcmd->argv);
	newcmd->nice = cmd->nice;

	for (i = 0; i < cmd->argc; ++i)
		newcmd->argv[i] = xstrdup (cmd->argv[i]);
	newcmd->argv[cmd->argc] = NULL;

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
	assert (cmd->argc < cmd->argv_max);
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

void command_argstr (command *cmd, const char *argstr)
{
	char *arg;

	while ((arg = argstr_get_word (&argstr))) {
		command_arg (cmd, arg);
		free (arg);
	}
}

void command_free (command *cmd)
{
	int i;

	if (!cmd)
		return;

	free (cmd->name);
	for (i = 0; i < cmd->argc; ++i)
		free (cmd->argv[i]);
	free (cmd->argv);
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
	p->infd = p->outfd = -1;
	p->infile = p->outfile = NULL;
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
	p->want_out = p2->want_out;
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
	int i, j;

	for (i = 0; i < p->ncommands; ++i) {
		fputs (p->commands[i]->name, stream);
		for (j = 1; j < p->commands[i]->argc; ++j) {
			/* TODO: escape_shell()? */
			putc (' ', stream);
			fputs (p->commands[i]->argv[j], stream);
		}
		if (i < p->ncommands - 1)
			fputs (" | ", stream);
	}
	fprintf (stream, " [input: %d, output: %d]\n",
		 p->want_in, p->want_out);
}

char *pipeline_tostring (pipeline *p)
{
	char *out = NULL;
	int i, j;

	for (i = 0; i < p->ncommands; ++i) {
		out = strappend (out, p->commands[i]->name, NULL);
		for (j = 1; j < p->commands[i]->argc; ++j)
			/* TODO: escape_shell()? */
			out = strappend (out, " ", p->commands[i]->argv[j],
					 NULL);
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

/* Children exit with this status if execvp fails. */
#define EXEC_FAILED_EXIT_STATUS 0xff

void pipeline_start (pipeline *p)
{
	int i;
	int last_input = -1;
	int infd[2];
	sigset_t set, oset;

	assert (!p->pids);	/* pipeline not started already */
	assert (!p->statuses);

	if (debug) {
		fputs ("Starting pipeline: ", stderr);
		pipeline_dump (p, stderr);
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

	for (i = 0; i < p->ncommands; i++) {
		int pdes[2];
		pid_t pid;
		int output_read = -1, output_write = -1;
		sigset_t set, oset;

		if (i != p->ncommands - 1 || p->want_out < 0) {
			if (pipe (pdes) < 0)
				error (FATAL, errno, _("pipe failed"));
			if (i == p->ncommands - 1)
				p->outfd = pdes[0];
			output_read = pdes[0];
			output_write = pdes[1];
		} else if (i == p->ncommands - 1 && p->want_out > 0)
			output_write = p->want_out;

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

			if (p->commands[i]->nice)
				nice (p->commands[i]->nice);

			execvp (p->commands[i]->name, p->commands[i]->argv);
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

		if (debug)
			fprintf (stderr, "Started \"%s\", pid %d\n",
				 p->commands[i]->name, pid);
	}
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
		if (fclose (p->outfile))
			error (0, errno,
			       _("closing pipeline output stream failed"));
		p->outfile = NULL;
		p->outfd = -1;
	} else if (p->outfd != -1) {
		if (close (p->outfd))
			error (0, errno, _("closing pipeline output failed"));
		p->outfd = -1;
	}

	while (proc_count > 0) {
		int r;

		if (debug)
			fprintf (stderr, "Active processes (%d):\n",
				 proc_count);

		/* Check for any statuses already collected by SIGCHLD
		 * handlers or the previous iteration before calling
		 * reap_children() again.
		 */
		for (i = 0; i < p->ncommands; ++i) {
			int status;

			if (p->pids[i] == -1)
				continue;

			if (debug)
				fprintf (stderr, "  \"%s\" (%d) -> %d\n",
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
				if (sig != SIGPIPE) {
#endif /* SIGPIPE */
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

		/* Tell the SIGCHLD handler not to get in our way. */
		queue_sigchld = 1;
		r = reap_children (1);
		queue_sigchld = 0;

		if (r == -1 && errno == ECHILD)
			/* Eh? The pipeline was allegedly still running, so
			 * we shouldn't have got ECHILD.
			 */
			error (FATAL, errno, _("waitpid failed"));
	}

	for (i = 0; i < n_active_pipelines; ++i)
		if (active_pipelines[i] == p)
			active_pipelines[i] = NULL;

	free (p->pids);
	p->pids = NULL;
	free (p->statuses);
	p->statuses = NULL;

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
	while (sigaction (SIGCHLD, &act, NULL) == -1) {
		if (errno == EINTR)
			continue;
		error (FATAL, errno, _("can't install SIGCHLD handler"));
	}
}
