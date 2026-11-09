/* Exercise native ptm/ptyfs, terminal line disciplines and controlling sessions. */
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "native pty failure at line %d errno=%d\n", __LINE__, errno); \
    return 1; } } while (0)

static int
read_exact(int fd, char *text, size_t size)
{
    size_t done = 0;
    while (done < size) {
        struct pollfd event = { .fd = fd, .events = POLLIN };
        if (poll(&event, 1, 30000) != 1)
            return 0;
        ssize_t amount = read(fd, text + done, size - done);
        if (amount <= 0)
            return 0;
        done += amount;
    }
    return 1;
}

int
main(void)
{
    int master, slave;
    char name[128], text[128];
    struct winsize window = { .ws_row = 37, .ws_col = 91 };
    CHECK(openpty(&master, &slave, name, NULL, &window) == 0);
    CHECK(strncmp(name, "/dev/pts/", 9) == 0 && isatty(master) && isatty(slave));
    struct stat st;
    CHECK(stat(name, &st) == 0 && S_ISCHR(st.st_mode) && (st.st_mode & 0777) == 0620);
    struct winsize actual;
    CHECK(ioctl(slave, TIOCGWINSZ, &actual) == 0 && actual.ws_row == 37 && actual.ws_col == 91);
    struct termios original, raw;
    CHECK(tcgetattr(slave, &original) == 0);
    raw = original;
    cfmakeraw(&raw);
    CHECK(tcsetattr(slave, TCSANOW, &raw) == 0);
    CHECK(write(master, "raw\0bytes", 9) == 9 && read_exact(slave, text, 9));
    CHECK(memcmp(text, "raw\0bytes", 9) == 0);
    CHECK(write(slave, "reply\n", 6) == 6 && read_exact(master, text, 6));
    CHECK(memcmp(text, "reply\n", 6) == 0);
    original.c_lflag |= ICANON;
    original.c_lflag &= ~ECHO;
    CHECK(tcsetattr(slave, TCSANOW, &original) == 0);
    CHECK(write(master, "wait", 4) == 4);
    struct pollfd event = { .fd = slave, .events = POLLIN };
    CHECK(poll(&event, 1, 0) == 0);
    CHECK(write(master, "\n", 1) == 1);
    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(slave, &reads);
    struct timeval timeout = { .tv_sec = 5 };
    CHECK(select(slave+1, &reads, NULL, NULL, &timeout) == 1 && FD_ISSET(slave, &reads));
    CHECK(read_exact(slave, text, 5) && memcmp(text, "wait\n", 5) == 0);
    CHECK(close(slave) == 0 && close(master) == 0);

    /* forkpty must create a session, controlling tty and stdio descriptors. */
    pid_t child = forkpty(&master, name, &raw, &window);
    CHECK(child >= 0);
    if (child == 0) {
        int tty = open("/dev/tty", O_RDWR);
        if (tty < 0 || getsid(0) != getpid() || tcgetpgrp(tty) != getpgrp() ||
            ioctl(tty, TIOCGWINSZ, &actual) != 0 || actual.ws_col != 91)
            _exit(11);
        (void)close(tty);
        if (write(1, "ready\n", 6) != 6 || !read_exact(0, text, 4) || memcmp(text, "ping", 4))
            _exit(12);
        /* Keep the slave open until the master has consumed its output. */
        execl("/bin/sh", "sh", "-c",
            "printf 'child:%s\\n' \"$((6*7))\"; read answer; [ \"$answer\" = done ]", NULL);
        _exit(13);
    }
    CHECK(read_exact(master, text, 6) && memcmp(text, "ready\n", 6) == 0);
    CHECK(write(master, "ping", 4) == 4);
    memset(text, 0, sizeof(text));
    int received = read_exact(master, text, 9);
    if (!received || memcmp(text, "child:42\n", 9)) {
        int child_status = 0;
        pid_t exited = waitpid(child, &child_status, WNOHANG);
        fprintf(stderr, "native pty child: read=%d wait=%ld status=%d bytes=", received,
            (long)exited, child_status);
        for (unsigned i = 0; i < 9; i++)
            fprintf(stderr, "%02x", (unsigned char)text[i]);
        fputc('\n', stderr);
        return 1;
    }
    CHECK(write(master, "done\n", 5) == 5);
    int status;
    CHECK(waitpid(child, &status, 0) == child && status == 0 && close(master) == 0);
    puts("RVM native pty: allocation, permissions, raw/canonical I/O, poll/select, forkpty and controlling shell verified");
    return 0;
}
