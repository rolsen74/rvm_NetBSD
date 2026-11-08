/* Small bring-up command monitor. Full NetBSD sh still needs libc/csu work. */
#include "rvm_user.h"
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ttycom.h>

char monitor_line[256], monitor_data[512];
char *monitor_argv[17];

static void
text(const char *s)
{
	size_t left = strlen(s);
	while (left) {
		ssize_t n = write(1, s, left);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return;
		left -= n; s += n;
	}
}
static void
number(unsigned value)
{
	char digits[16];
	unsigned n = 0;
	do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
	while (n) write(1, &digits[--n], 1);
}
static void
file(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) { puts("open failed"); return; }
	for (;;) {
		ssize_t n = read(fd, monitor_data, sizeof(monitor_data));
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) break;
		for (ssize_t done = 0; done < n;) {
			ssize_t sent = write(1, monitor_data + done, n - done);
			if (sent <= 0) break;
			done += sent;
		}
	}
	close(fd);
}

int main(int, char **, char **);
int
main(int argc __attribute__((unused)), char **argv __attribute__((unused)),
    char **envp __attribute__((unused)))
{
	/* The root image's PID 1 already owns descriptors 0, 1 and 2. */
	int foreground;
	if ((setsid() < 0 && errno != EPERM) || ioctl(0, TIOCSCTTY, 0) < 0 ||
	    ioctl(0, TIOCGPGRP, &foreground) < 0 || foreground != getpid())
		puts("console controlling tty setup failed");
	int controlling = open("/dev/tty", O_RDWR);
	if (controlling < 0 || ioctl(controlling, TIOCGPGRP, &foreground) < 0 || foreground != getpid())
		puts("console controlling tty setup failed");
	if (controlling >= 0) close(controlling);
	if ((!getenv("PATH") && setenv("PATH", "/bin:/usr/bin", 0)) ||
	    (!getenv("TERM") && setenv("TERM", "dumb", 0)))
		puts("environment setup failed");
	puts("NetBSD/RVM console monitor ready");
	puts("Commands: help, pid, env, set NAME VALUE, unset NAME, echo TEXT, cat /PATH, run PROGRAM [ARGS]");
	for (;;) {
		text("rvm# ");
		ssize_t n;
		do { n = read(0, monitor_line, sizeof(monitor_line) - 1); }
		while (n < 0 && errno == EINTR);
		if (n <= 0) {
			struct timespec delay = { 0, 100000000 };
			nanosleep(&delay, 0);
			continue;
		}
		monitor_line[n] = 0;
		unsigned count = 0;
		char *p = monitor_line;
		while (*p && count < 16) {
			while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
			if (!*p) break;
			monitor_argv[count++] = p;
			while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
			if (*p) *p++ = 0;
		}
		monitor_argv[count] = 0;
		if (count == 0) continue;
		if (!strcmp(monitor_argv[0], "help")) {
			puts("help | pid | env | set NAME VALUE | unset NAME | echo TEXT | cat /PATH | run PROGRAM [ARGS]");
		} else if (!strcmp(monitor_argv[0], "pid")) {
			text("monitor pid="); number(getpid()); text("\n");
		} else if (!strcmp(monitor_argv[0], "echo")) {
			for (unsigned i = 1; i < count; i++) {
				if (i > 1) text(" ");
				text(monitor_argv[i]);
			}
			text("\n");
		} else if (!strcmp(monitor_argv[0], "cat") && count == 2) {
			file(monitor_argv[1]);
		} else if (!strcmp(monitor_argv[0], "env")) {
			for (char **e = environ; *e; e++) puts(*e);
		} else if (!strcmp(monitor_argv[0], "set") && count == 3) {
			puts(setenv(monitor_argv[1], monitor_argv[2], 1) ? "set failed" : "environment updated");
		} else if (!strcmp(monitor_argv[0], "unset") && count == 2) {
			puts(unsetenv(monitor_argv[1]) ? "unset failed" : "environment updated");
		} else if (!strcmp(monitor_argv[0], "run") && count >= 2) {
			pid_t pid = fork();
			if (pid == 0) {
				execvp(monitor_argv[1], &monitor_argv[1]);
				puts("exec failed");
				_exit(127);
			}
			int status;
			if (pid < 0 || waitpid(pid, &status, 0) != pid) {
				puts("fork/wait failed");
			} else {
				text("status="); number((unsigned)status); text("\n");
			}
		} else {
			puts("Unknown command; type help.");
		}
	}
}
