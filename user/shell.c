/**
 * @file shell.c
 * @brief Command-line shell for Serotonin OS
 *
 * Provides an interactive shell with command history, line editing,
 * PATH-based command lookup, pipelines, I/O redirection, and
 * environment variables.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <lib5ht.h>

int gethostname(char *name, size_t len);
int snprintf(char *str, size_t size, const char *fmt, ...);

/* ── History ─────────────────────────────────────────────────────── */

#define COMMAND_MAX  256
#define HISTORY_MAX  64

static char  history[HISTORY_MAX][COMMAND_MAX];
static int   history_count;          /* total entries stored */
static int   history_wpos;           /* next write slot (circular) */

static void history_add(const char *cmd)
{
	if (cmd[0] == '\0')
		return;
	/* skip duplicates of the most recent entry */
	int last = (history_wpos - 1 + HISTORY_MAX) % HISTORY_MAX;
	if (history_count > 0 && strcmp(history[last], cmd) == 0)
		return;
	size_t clen = strlen(cmd);
	if (clen >= COMMAND_MAX) clen = COMMAND_MAX - 1;
	memcpy(history[history_wpos], cmd, clen);
	history[history_wpos][clen] = '\0';
	history_wpos = (history_wpos + 1) % HISTORY_MAX;
	if (history_count < HISTORY_MAX)
		history_count++;
}

/* Map a logical history index (0 = oldest) to the circular buffer slot */
static const char *history_get(int idx)
{
	int real = (history_wpos - history_count + idx + HISTORY_MAX) % HISTORY_MAX;
	return history[real];
}

/* ── Environment variables ───────────────────────────────────────── */

#define MAX_ENV     64
#define ENV_MAXLEN  256

static char  env_store[MAX_ENV][ENV_MAXLEN];
static char *shell_env[MAX_ENV + 1];  /* NULL-terminated for execve */
static int   env_count;

static void env_init(char **envp)
{
	env_count = 0;
	for (char **e = envp; e && *e && env_count < MAX_ENV; e++) {
		strncpy(env_store[env_count], *e, ENV_MAXLEN - 1);
		env_store[env_count][ENV_MAXLEN - 1] = '\0';
		shell_env[env_count] = env_store[env_count];
		env_count++;
	}
	shell_env[env_count] = NULL;
}

/* Find index of VAR in env, or -1 */
static int env_find(const char *name, size_t namelen)
{
	for (int i = 0; i < env_count; i++) {
		if (strncmp(shell_env[i], name, namelen) == 0 &&
		    shell_env[i][namelen] == '=')
			return i;
	}
	return -1;
}

/* Set VAR=value. Returns 0 on success, -1 if full. */
static int env_set(const char *assignment)
{
	const char *eq = strchr(assignment, '=');
	if (!eq) return -1;
	size_t namelen = (size_t)(eq - assignment);

	int idx = env_find(assignment, namelen);
	if (idx >= 0) {
		strncpy(env_store[idx], assignment, ENV_MAXLEN - 1);
		env_store[idx][ENV_MAXLEN - 1] = '\0';
		return 0;
	}
	if (env_count >= MAX_ENV) return -1;
	strncpy(env_store[env_count], assignment, ENV_MAXLEN - 1);
	env_store[env_count][ENV_MAXLEN - 1] = '\0';
	shell_env[env_count] = env_store[env_count];
	env_count++;
	shell_env[env_count] = NULL;
	return 0;
}

/* Remove VAR from env */
static void env_unset(const char *name)
{
	size_t namelen = strlen(name);
	int idx = env_find(name, namelen);
	if (idx < 0) return;
	/* shift remaining entries down */
	for (int i = idx; i < env_count - 1; i++) {
		strcpy(env_store[i], env_store[i + 1]);
		shell_env[i] = env_store[i];
	}
	env_count--;
	shell_env[env_count] = NULL;
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static void write_str(int fd, const char *s)
{
	write(fd, s, strlen(s));
}

/* Safe string copy that avoids strncpy truncation warnings */
static void safe_copy(char *dst, const char *src, size_t dstsize)
{
	size_t slen = strlen(src);
	if (slen >= dstsize) slen = dstsize - 1;
	memcpy(dst, src, slen);
	dst[slen] = '\0';
}

/* Clear the visible line: move cursor to col 0, overwrite with spaces,
   then move back.  Works on a simple single-line basis. */
static void clear_line(int len, int cursor)
{
	while (cursor > 0) { write(1, "\b", 1); cursor--; }
	for (int i = 0; i < len; i++) write(1, " ", 1);
	for (int i = 0; i < len; i++) write(1, "\b", 1);
}

/* ── Line editor (raw-mode) ──────────────────────────────────────── */

static int read_line(char *buf, int bufsize)
{
	int len    = 0;
	int cursor = 0;
	int hist_browse = -1;          /* -1 = editing current input */
	char saved_input[COMMAND_MAX]; /* stash current text while browsing */
	saved_input[0] = '\0';

	/* Switch to raw mode so we get individual keystrokes */
	pty_attr_t orig, raw;
	ioctl(0, TCGETS, &orig);
	raw = orig;
	raw.echo   = 0;
	raw.icanon = 0;
	ioctl(0, TCSETS, &raw);

	for (;;) {
		char c;
		int n = read(0, &c, 1);
		if (n <= 0) {                  /* EOF */
			ioctl(0, TCSETS, &orig);
			return -1;
		}

		/* ── Enter ────────────────────────────────────────── */
		if (c == '\n' || c == '\r') {
			write(1, "\n", 1);
			break;
		}

		/* ── Escape sequences (arrows) ────────────────────── */
		if (c == 27) {
			char seq[2];
			if (read(0, &seq[0], 1) <= 0) continue;
			if (read(0, &seq[1], 1) <= 0) continue;
			if (seq[0] != '[') continue;

			if (seq[1] == 'A') {               /* Up arrow */
				if (history_count == 0) continue;
				if (hist_browse == -1) {
					memcpy(saved_input, buf, len);
					saved_input[len] = '\0';
					hist_browse = history_count - 1;
				} else if (hist_browse > 0) {
					hist_browse--;
				} else {
					continue;          /* already at oldest */
				}
				clear_line(len, cursor);
				safe_copy(buf, history_get(hist_browse), bufsize);
				len = strlen(buf);
				cursor = len;
				write(1, buf, len);

			} else if (seq[1] == 'B') {        /* Down arrow */
				if (hist_browse == -1) continue;
				clear_line(len, cursor);
				if (hist_browse >= history_count - 1) {
					hist_browse = -1;
					safe_copy(buf, saved_input, bufsize);
				} else {
					hist_browse++;
					safe_copy(buf, history_get(hist_browse), bufsize);
				}
				len = strlen(buf);
				cursor = len;
				write(1, buf, len);

			} else if (seq[1] == 'C') {        /* Right arrow */
				if (cursor < len) {
					write(1, "\033[C", 3);
					cursor++;
				}
			} else if (seq[1] == 'D') {        /* Left arrow */
				if (cursor > 0) {
					write(1, "\033[D", 3);
					cursor--;
				}
			}
			continue;
		}

		/* ── Backspace (127 or 8) ─────────────────────────── */
		if (c == 127 || c == 8) {
			if (cursor > 0) {
				memmove(buf + cursor - 1, buf + cursor, len - cursor);
				len--;
				cursor--;
				write(1, "\b", 1);
				write(1, buf + cursor, len - cursor);
				write(1, " \b", 2);
				int back = len - cursor;
				for (int i = 0; i < back; i++) write(1, "\b", 1);
			}
			continue;
		}

		/* ── Ctrl+C ───────────────────────────────────────── */
		if (c == 3) {
			write(1, "^C\n", 3);
			buf[0] = '\0';
			ioctl(0, TCSETS, &orig);
			return 0;
		}

		/* ── Ctrl+D on empty line = EOF ───────────────────── */
		if (c == 4) {
			if (len == 0) {
				ioctl(0, TCSETS, &orig);
				return -1;
			}
			continue;
		}

		/* ── Ctrl+U — kill line ───────────────────────────── */
		if (c == 21) {
			clear_line(len, cursor);
			len = 0;
			cursor = 0;
			continue;
		}

		/* ── Ctrl+W — kill word ───────────────────────────── */
		if (c == 23) {
			if (cursor == 0) continue;
			int old = cursor;
			while (cursor > 0 && buf[cursor - 1] == ' ') cursor--;
			while (cursor > 0 && buf[cursor - 1] != ' ') cursor--;
			int removed = old - cursor;
			memmove(buf + cursor, buf + old, len - old);
			len -= removed;
			/* redraw */
			for (int i = 0; i < old - cursor; i++) write(1, "\b", 1);
			write(1, buf + cursor, len - cursor);
			for (int i = 0; i < removed; i++) write(1, " ", 1);
			int back = len - cursor + removed;
			for (int i = 0; i < back; i++) write(1, "\b", 1);
			continue;
		}

		/* ── Printable character ──────────────────────────── */
		if (c >= 32 && len < bufsize - 1) {
			if (cursor < len)
				memmove(buf + cursor + 1, buf + cursor, len - cursor);
			buf[cursor] = c;
			len++;
			cursor++;
			write(1, buf + cursor - 1, len - cursor + 1);
			int back = len - cursor;
			for (int i = 0; i < back; i++) write(1, "\b", 1);
		}
	}

	buf[len] = '\0';
	ioctl(0, TCSETS, &orig);
	return len;
}

/* ── Username lookup ─────────────────────────────────────────────── */

static void get_username(uid_t uid, char *out, size_t outsize)
{
	int fd = open("/etc/passwd", 0);
	if (fd < 0) { strncpy(out, "?", outsize); return; }

	char buf[1024];
	int n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0) { strncpy(out, "?", outsize); return; }
	buf[n] = '\0';

	// cppcheck-suppress constVariablePointer
	char *line = buf;
	while (line < buf + n) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';

		if (line[0] != '\0' && line[0] != '#') {
			// cppcheck-suppress constVariablePointer
			char *c1 = strchr(line, ':');
			if (c1) {
				char *c2 = strchr(c1 + 1, ':');
				if (c2) {
					int entry_uid = 0;
					char *p = c2 + 1;
					while (*p >= '0' && *p <= '9')
						entry_uid = entry_uid * 10 + (*p++ - '0');
					if (entry_uid == (int)uid) {
						size_t ulen = (size_t)(c1 - line);
						if (ulen >= outsize) ulen = outsize - 1;
						memcpy(out, line, ulen);
						out[ulen] = '\0';
						return;
					}
				}
			}
		}
		if (!nl) break;
		line = nl + 1;
	}
	strncpy(out, "?", outsize);
}

/* ── Prompt colours (pastel, 256-colour) ─────────────────────────── */

#define COL_USER   "\033[38;5;114m"   /* soft green  */
#define COL_AT     "\033[38;5;250m"   /* light grey  */
#define COL_HOST   "\033[38;5;150m"   /* sage green  */
#define COL_PATH   "\033[38;5;111m"   /* pastel blue */
#define COL_PROMPT "\033[38;5;183m"   /* pastel lilac */
#define COL_RESET  "\033[0m"

static void sigint_handle(int sig) { (void)sig; }

/* ── Pipeline support ────────────────────────────────────────────── */

#define MAX_PIPELINE 8
#define MAX_ARGS     64

typedef struct {
	char *args[MAX_ARGS];
	int   argc;
} stage_t;

/*
 * Split a tokenized args array into pipeline stages on '|'.
 * Returns the number of stages (1 = no pipes).
 */
static int split_pipeline(char **args, int argc, stage_t *stages)
{
	int nstages = 0;
	stages[0].argc = 0;

	for (int i = 0; i < argc; i++) {
		if (strcmp(args[i], "|") == 0) {
			if (stages[nstages].argc == 0) {
				write_str(2, "shell: syntax error near '|'\n");
				return -1;
			}
			stages[nstages].args[stages[nstages].argc] = NULL;
			nstages++;
			if (nstages >= MAX_PIPELINE) {
				write_str(2, "shell: too many pipeline stages\n");
				return -1;
			}
			stages[nstages].argc = 0;
		} else {
			if (stages[nstages].argc < MAX_ARGS - 1)
				stages[nstages].args[stages[nstages].argc++] = args[i];
		}
	}

	if (stages[nstages].argc == 0) {
		write_str(2, "shell: syntax error near '|'\n");
		return -1;
	}
	stages[nstages].args[stages[nstages].argc] = NULL;
	nstages++;
	return nstages;
}

/*
 * Process I/O redirections in a stage's args.
 * Opens files and dup2's to stdin/stdout as needed.
 * Removes redirection tokens from args in-place.
 * Called in the child process after fork, before exec.
 * Returns 0 on success, -1 on error.
 */
static int setup_redirections(stage_t *stage)
{
	int out = 0;
	for (int i = 0; i < stage->argc; i++) {
		// cppcheck-suppress constVariablePointer
		char *tok = stage->args[i];
		int mode = -1;
		int target_fd = -1;

		if (strcmp(tok, ">") == 0) {
			mode = O_CREAT | O_WRONLY | O_TRUNC;
			target_fd = 1;
		} else if (strcmp(tok, ">>") == 0) {
			mode = O_CREAT | O_WRONLY | O_APPEND;
			target_fd = 1;
		} else if (strcmp(tok, "<") == 0) {
			mode = O_RDONLY;
			target_fd = 0;
		}

		if (mode >= 0) {
			if (i + 1 >= stage->argc) {
				write_str(2, "shell: missing filename for redirection\n");
				return -1;
			}
			char *filename = stage->args[i + 1];
			int fd = open(filename, mode, 0644);
			if (fd < 0) {
				printf("shell: %s: cannot open (errno=%d)\n", filename, errno);
				return -1;
			}
			dup2(fd, target_fd);
			close(fd);
			i++; /* skip the filename */
			continue;
		}

		/* keep this arg */
		stage->args[out++] = stage->args[i];
	}
	stage->argc = out;
	stage->args[out] = NULL;
	return 0;
}

/*
 * Resolve a command name to a full path via PATH lookup.
 * Writes the result into 'out' (size outsize).
 * Returns 0 on success (out is filled), -1 if not found.
 */
static int resolve_command(const char *cmd, char *out, size_t outsize)
{
	if (strchr(cmd, '/')) {
		strncpy(out, cmd, outsize - 1);
		out[outsize - 1] = '\0';
		return 0;
	}

	/* search PATH */
	const char *path_env = NULL;
	for (int i = 0; i < env_count; i++) {
		if (strncmp(shell_env[i], "PATH=", 5) == 0) {
			path_env = shell_env[i] + 5;
			break;
		}
	}
	if (!path_env || path_env[0] == '\0')
		path_env = "/bin";

	const char *pp = path_env;
	while (*pp) {
		const char *start = pp;
		while (*pp && *pp != ':') pp++;
		size_t dir_len = (size_t)(pp - start);
		size_t cmd_len = strlen(cmd);

		if (dir_len == 0) {
			if (cmd_len + 1 < outsize) {
				memcpy(out, cmd, cmd_len);
				out[cmd_len] = '\0';
				return 0;
			}
		} else if (dir_len + 1 + cmd_len + 1 < outsize) {
			memcpy(out, start, dir_len);
			if (out[dir_len - 1] != '/')
				out[dir_len++] = '/';
			memcpy(out + dir_len, cmd, cmd_len);
			out[dir_len + cmd_len] = '\0';
			/* check if it exists via stat */
			struct stat st;
			if (stat(out, &st) == 0)
				return 0;
		}

		if (*pp == ':') pp++;
	}
	return -1;
}

/*
 * Execute a pipeline of nstages commands.
 * Handles fork, pipe creation, redirection, and exec.
 */
static void exec_pipeline(stage_t *stages, int nstages, int background)
{
	int pipefds[MAX_PIPELINE - 1][2];
	pid_t pids[MAX_PIPELINE];

	/* create all needed pipes */
	for (int i = 0; i < nstages - 1; i++) {
		if (pipe(pipefds[i]) < 0) {
			write_str(2, "shell: pipe failed\n");
			return;
		}
	}

	for (int i = 0; i < nstages; i++) {
		pid_t pid = fork();
		if (pid < 0) {
			write_str(2, "shell: fork failed\n");
			/* close remaining pipes */
			for (int j = 0; j < nstages - 1; j++) {
				close(pipefds[j][0]);
				close(pipefds[j][1]);
			}
			return;
		}

		if (pid == 0) {
			/* ── Child ─────────────────────────────────── */
			sys_5ht_pty_setpgrp(0);

			/* wire up pipes */
			if (i > 0) {
				dup2(pipefds[i - 1][0], 0);  /* stdin from prev pipe */
			}
			if (i < nstages - 1) {
				dup2(pipefds[i][1], 1);  /* stdout to next pipe */
			}

			/* close all pipe fds in child */
			for (int j = 0; j < nstages - 1; j++) {
				close(pipefds[j][0]);
				close(pipefds[j][1]);
			}

			/* handle redirections (>, >>, <) */
			if (setup_redirections(&stages[i]) < 0)
				_exit(1);

			if (stages[i].argc == 0)
				_exit(0);

			/* resolve and exec */
			char candidate[256];
			if (resolve_command(stages[i].args[0], candidate, sizeof(candidate)) == 0) {
				execve(candidate, stages[i].args, shell_env);
			}

			printf("%s: command not found\n", stages[i].args[0]);
			_exit(127);
		}

		pids[i] = pid;
	}

	/* ── Parent: close all pipe fds ──────────────────── */
	for (int j = 0; j < nstages - 1; j++) {
		close(pipefds[j][0]);
		close(pipefds[j][1]);
	}

	/* wait for all children */
	if (!background) {
		for (int i = 0; i < nstages; i++) {
			int status;
			waitpid(pids[i], &status, 0);
		}
		sys_5ht_pty_setpgrp(0);    /* reclaim foreground */
	} else {
		printf("[%d]\n", pids[nstages - 1]);
	}
}

/* ── Check if a string is a valid variable assignment (VAR=value) ── */

static int is_var_assignment(const char *s)
{
	if (!s || !((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '_'))
		return 0;
	const char *p = s + 1;
	while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
	       (*p >= '0' && *p <= '9') || *p == '_')
		p++;
	return (*p == '=');
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv;

	sys_5ht_pty_setpgrp(0);        /* shell is foreground */
	signal(SIGINT, sigint_handle);

	/* initialize shell environment from inherited envp */
	env_init(envp);

	char username[32];
	get_username(getuid(), username, sizeof(username));

	char hostname[65];
	if (gethostname(hostname, sizeof(hostname)) < 0)
		strcpy(hostname, "serotonin");

	for (;;) {
		/* ── Print prompt ─────────────────────────────────── */
		char cwd[256];
		// cppcheck-suppress variableScope
		char prompt_buf[512];
		if (getcwd(cwd, sizeof(cwd))) {
			int len = snprintf(prompt_buf, sizeof(prompt_buf),
				COL_USER "%s" COL_AT "@" COL_HOST "%s "
				COL_PATH "%s " COL_PROMPT "> " COL_RESET,
				username, hostname, cwd);
			if (len > 0) write(1, prompt_buf, len);
		} else {
			write_str(1, COL_PROMPT "serotonin> " COL_RESET);
		}

		/* ── Read command (with line editing + history) ──── */
		char command[COMMAND_MAX];
		int count = read_line(command, sizeof(command));

		if (count < 0)                 /* EOF */
			break;
		if (count == 0 || command[0] == '\0')
			continue;

		history_add(command);

		/* ── Trim trailing whitespace ─────────────────────── */
		int end = strlen(command) - 1;
		while (end >= 0 && (command[end] == ' ' || command[end] == '\t'))
			command[end--] = '\0';

		/* ── Check for background '&' ─────────────────────── */
		int background = 0;
		if (end >= 0 && command[end] == '&') {
			background = 1;
			command[end--] = '\0';
			while (end >= 0 && (command[end] == ' ' || command[end] == '\t'))
				command[end--] = '\0';
		}

		/* ── Tokenise ─────────────────────────────────────── */
		char *args[MAX_ARGS];
		int arg_count = 0;
		char *p = command;
		while (*p) {
			while (*p == ' ' || *p == '\t') p++;
			if (*p == '\0') break;
			if (arg_count < MAX_ARGS - 1)
				args[arg_count++] = p;
			while (*p && *p != ' ' && *p != '\t') p++;
			if (*p) *p++ = '\0';
		}
		args[arg_count] = NULL;
		if (arg_count == 0)
			continue;

		/* ── Variable assignment: VAR=value ───────────────── */
		if (arg_count == 1 && is_var_assignment(args[0])) {
			env_set(args[0]);
			continue;
		}

		/* ── Built-in: export VAR=value ──────────────────── */
		if (strcmp(args[0], "export") == 0) {
			for (int i = 1; i < arg_count; i++) {
				if (is_var_assignment(args[i]))
					env_set(args[i]);
				else
					printf("export: invalid: %s\n", args[i]);
			}
			continue;
		}

		/* ── Built-in: unset VAR ─────────────────────────── */
		if (strcmp(args[0], "unset") == 0) {
			for (int i = 1; i < arg_count; i++)
				env_unset(args[i]);
			continue;
		}

		/* ── Built-in: cd ─────────────────────────────────── */
		if (strcmp(args[0], "cd") == 0) {
			const char *target = (arg_count > 1) ? args[1] : "/";
			if (chdir(target) != 0)
				printf("cd: %s: no such directory\n", target);
			continue;
		}

		/* ── Built-in: clear ──────────────────────────────── */
		if (strcmp(args[0], "clear") == 0) {
			write(1, "\033[2J\033[H", 7);
			continue;
		}

		/* ── Built-in: exit ───────────────────────────────── */
		if (strcmp(args[0], "exit") == 0) {
			int code = (arg_count > 1) ? atoi(args[1]) : 0;
			_exit(code);
		}

		/* ── Split into pipeline stages ───────────────────── */
		stage_t stages[MAX_PIPELINE];
		int nstages = split_pipeline(args, arg_count, stages);
		if (nstages < 0)
			continue;  /* parse error */

		/* ── Execute pipeline ─────────────────────────────── */
		exec_pipeline(stages, nstages, background);
	}

	_exit(0);
}
