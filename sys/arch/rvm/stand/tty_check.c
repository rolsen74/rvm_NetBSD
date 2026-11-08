/* Exercise the real console line discipline through user system calls.
 * TIOCSTI supplies deterministic input in headless tests; normal keyboard
 * events use the driver's callout and the same MI input path.
 */
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/termios.h>
#include <sys/filio.h>
#include <sys/poll.h>
#include <sys/errno.h>

extern long rvm_call(long, long, long, long, long);
int tty_check(void);
char tty_path[] = "/dev/console";
char tty_message[] = "NetBSD/RVM: tty termios, canonical editing, EOF, raw input, poll and blocking wakeup verified\n";
struct termios tty_saved, tty_mode;
struct winsize tty_size;
struct pollfd tty_pollfd;
char tty_input[32];
int tty_nonblock, tty_status;
long tty_delay[2] = { 0, 20000000 };

static int
inject(long fd, unsigned char ch)
{
	return rvm_call(SYS_ioctl, fd, TIOCSTI, (long)&ch, 0);
}

int
tty_check(void)
{
	long fd = rvm_call(SYS_open, (long)tty_path, 2, 0, 0);
	if (fd < 0 || rvm_call(SYS_ioctl, fd, TIOCGETA, (long)&tty_saved, 0) != 0 ||
	    rvm_call(SYS_ioctl, fd, TIOCGWINSZ, (long)&tty_size, 0) != 0 ||
	    tty_size.ws_col != 100 || tty_size.ws_row != 75)
		return 110;
	tty_mode = tty_saved;
	tty_mode.c_lflag &= ~(ECHO | ECHONL);
	tty_mode.c_lflag |= ICANON;
	if (rvm_call(SYS_ioctl, fd, TIOCSETA, (long)&tty_mode, 0) != 0)
		return 111;
	tty_nonblock = 1;
	if (rvm_call(SYS_ioctl, fd, FIONBIO, (long)&tty_nonblock, 0) != 0 ||
	    rvm_call(SYS_read, fd, (long)tty_input, sizeof(tty_input), 0) != -EWOULDBLOCK)
		return 112;
	tty_pollfd.fd = fd;
	tty_pollfd.events = POLLIN;
	if (rvm_call(SYS_poll, (long)&tty_pollfd, 1, 0, 0) != 0)
		return 113;
	if (inject(fd, 'a') || inject(fd, 'b') || inject(fd, 'c') ||
	    inject(fd, tty_mode.c_cc[VERASE]) || inject(fd, 'd') || inject(fd, '\n'))
		return 114;
	if (rvm_call(SYS_poll, (long)&tty_pollfd, 1, 0, 0) != 1 ||
	    !(tty_pollfd.revents & POLLIN) ||
	    rvm_call(SYS_read, fd, (long)tty_input, sizeof(tty_input), 0) != 4 ||
	    tty_input[0] != 'a' || tty_input[1] != 'b' || tty_input[2] != 'd' || tty_input[3] != '\n')
		return 115;
	if (inject(fd, tty_mode.c_cc[VEOF]) ||
	    rvm_call(SYS_read, fd, (long)tty_input, sizeof(tty_input), 0) != 0)
		return 116;
	tty_mode.c_lflag &= ~(ICANON | ISIG);
	tty_mode.c_cc[VMIN] = 1;
	tty_mode.c_cc[VTIME] = 0;
	if (rvm_call(SYS_ioctl, fd, TIOCSETA, (long)&tty_mode, 0) != 0 || inject(fd, 'Z') ||
	    rvm_call(SYS_read, fd, (long)tty_input, sizeof(tty_input), 0) != 1 || tty_input[0] != 'Z')
		return 117;
	tty_nonblock = 0;
	if (rvm_call(SYS_ioctl, fd, FIONBIO, (long)&tty_nonblock, 0) != 0)
		return 118;
	long pid = rvm_call(SYS_fork, 0, 0, 0, 0);
	if (pid == 0) {
		rvm_call(SYS___nanosleep50, (long)tty_delay, 0, 0, 0);
		int error = inject(fd, 'K');
		rvm_call(SYS_exit, error != 0, 0, 0, 0);
		for (;;) { }
	}
	if (pid < 0 || rvm_call(SYS_read, fd, (long)tty_input, sizeof(tty_input), 0) != 1 ||
	    tty_input[0] != 'K' ||
	    rvm_call(SYS___wait450, pid, (long)&tty_status, 0, 0) != pid || tty_status != 0)
		return 119;
	if (rvm_call(SYS_ioctl, fd, TIOCSETA, (long)&tty_saved, 0) != 0 ||
	    rvm_call(SYS_close, fd, 0, 0, 0) != 0 ||
	    rvm_call(SYS_write, 1, (long)tty_message, sizeof(tty_message)-1, 0) < 0)
		return 120;
	return 0;
}
