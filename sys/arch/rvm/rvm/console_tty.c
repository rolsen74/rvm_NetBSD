/* GeneralDriver console tty. Hardware events are polled by a clock callout;
 * all character processing, input queues, echo, signals and termios use MI tty.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <sys/ttydefaults.h>
#include <sys/kauth.h>
#include <sys/callout.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <machine/boot.h>
#include <machine/console.h>
#include <machine/generic.h>

static struct tty *console_tty;
static callout_t input_callout;
static bool polling;
static bool opened;
static dev_type_open(rvmconsopen);
static dev_type_close(rvmconsclose);
static dev_type_read(rvmconsread);
static dev_type_write(rvmconswrite);
static dev_type_ioctl(rvmconsioctl);
static dev_type_tty(rvmconstty);
static dev_type_poll(rvmconspoll);

const struct cdevsw rvmcons_cdevsw = {
	.d_open = rvmconsopen, .d_close = rvmconsclose,
	.d_read = rvmconsread, .d_write = rvmconswrite,
	.d_ioctl = rvmconsioctl, .d_tty = rvmconstty,
	.d_stop = nostop, .d_poll = rvmconspoll,
	.d_mmap = nommap, .d_kqfilter = ttykqfilter,
	.d_discard = nodiscard, .d_flag = D_TTY,
};

/* Called with tty_lock held. Drain a bounded burst, then use tty's standard
 * restart callout so a large write cannot monopolize the CPU at IPL_TTY. */
static void
console_start(struct tty *tp)
{
	unsigned char data[128];
	int length;
	if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP))
		return;
	tp->t_state |= TS_BUSY;
	length = q_to_b(&tp->t_outq, data, sizeof(data));
	for (int i = 0; i < length; i++)
		rvm_early_putchar(data[i]);
	tp->t_state &= ~TS_BUSY;
	if (ttypull(tp)) {
		tp->t_state |= TS_TIMEOUT;
		callout_schedule(&tp->t_rstrt_ch, 1);
	}
}

static int
console_param(struct tty *tp, struct termios *settings)
{
	tp->t_ispeed = settings->c_ispeed;
	tp->t_ospeed = settings->c_ospeed;
	tp->t_cflag = settings->c_cflag;
	return 0;
}

static void
console_input(void *arg)
{
	struct tty *tp = arg;
	volatile struct rvm_general_driver *gd = (void *)RVM_GENERIC_ADDR;
	if (opened && !polling) {
		/* Each register read consumes an event, including key releases.
		 * Limit work even if the host continuously supplies keyboard data. */
		for (unsigned i = 0; i < 64; i++) {
			uint64_t event = gd->keyboard;
			unsigned key = event & 0xffff;
			if (event == UINT64_MAX)
				break;
			if (!(event & (1UL << 16)) || key == 0 || key >= 128)
				continue;
			/* SDL provides Backspace as ASCII BS; MI defaults use DEL. */
			if (key == '\b') key = 0x7f;
			(*tp->t_linesw->l_rint)(key, tp);
		}
	}
	if (opened)
		callout_schedule(&input_callout, 1);
}

void
rvm_console_poll(bool on)
{
	polling = on;
}

static int
rvmconsopen(dev_t dev, int flags, int mode __unused, struct lwp *l)
{
	struct tty *tp;
	int error;
	if (minor(dev) != 0)
		return ENXIO;
	if (console_tty == NULL) {
		console_tty = tty_alloc();
		console_tty->t_dev = dev;
		console_tty->t_oproc = console_start;
		console_tty->t_param = console_param;
		tty_attach(console_tty);
		callout_init(&input_callout, 0);
		callout_setfunc(&input_callout, console_input, console_tty);
	}
	tp = console_tty;
	if (kauth_authorize_device_tty(l->l_cred, KAUTH_DEVICE_TTY_OPEN, tp))
		return EBUSY;
	if (!(tp->t_state & TS_ISOPEN)) {
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG | CLOCAL;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
		ttsetwater(tp);
	}
	tp->t_state |= TS_CARR_ON;
	error = ttyopen(tp, 0, flags & O_NONBLOCK);
	if (error == 0)
		error = (*tp->t_linesw->l_open)(dev, tp);
	if (error == 0 && !opened) {
		/* The default line discipline clears the size on initial open. */
		tp->t_winsize.ws_col = RVM_TERM_COLUMNS;
		tp->t_winsize.ws_row = RVM_TERM_ROWS;
		opened = true;
		callout_schedule(&input_callout, 1);
	}
	return error;
}

static int
rvmconsclose(dev_t dev __unused, int flags, int mode __unused,
    struct lwp *l __unused)
{
	struct tty *tp = console_tty;
	if (tp == NULL || !(tp->t_state & TS_ISOPEN))
		return 0;
	opened = false;
	callout_stop(&input_callout);
	(*tp->t_linesw->l_close)(tp, flags);
	ttyclose(tp);
	return 0;
}
static int
rvmconsread(dev_t dev __unused, struct uio *uio, int flags)
{
	return (*console_tty->t_linesw->l_read)(console_tty, uio, flags);
}
static int
rvmconswrite(dev_t dev __unused, struct uio *uio, int flags)
{
	return (*console_tty->t_linesw->l_write)(console_tty, uio, flags);
}
static int
rvmconsioctl(dev_t dev __unused, u_long cmd, void *data, int flags, struct lwp *l)
{
	struct tty *tp = console_tty;
	int error = (*tp->t_linesw->l_ioctl)(tp, cmd, data, flags, l);
	return error != EPASSTHROUGH ? error : ttioctl(tp, cmd, data, flags, l);
}
static struct tty *
rvmconstty(dev_t dev)
{
	return minor(dev) == 0 ? console_tty : NULL;
}
static int
rvmconspoll(dev_t dev __unused, int events, struct lwp *l)
{
	return (*console_tty->t_linesw->l_poll)(console_tty, events, l);
}
