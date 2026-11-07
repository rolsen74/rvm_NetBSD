/* Bootstrap console descriptors backed by GeneralDriver character output.
 * This supplies a real fileops/write path without a root vnode or tty yet.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <dev/cons.h>
#include <machine/user.h>

static int
console_write(struct file *fp __unused, off_t *offset __unused, struct uio *uio,
    kauth_cred_t cred __unused, int flags __unused)
{
	char buffer[128];
	while (uio->uio_resid != 0) {
		size_t size = MIN(sizeof(buffer), uio->uio_resid);
		int error = uiomove(buffer, size, uio);
		if (error != 0) return error;
		for (size_t i = 0; i < size; i++) cnputc((unsigned char)buffer[i]);
	}
	return 0;
}
static int console_close(struct file *fp __unused) { return 0; }
static int
console_stat(struct file *fp __unused, struct stat *st)
{
	memset(st, 0, sizeof(*st));
	st->st_mode = S_IFCHR | 0600;
	st->st_nlink = 1;
	st->st_rdev = NODEV;
	st->st_blksize = PAGE_SIZE;
	return 0;
}
static const struct fileops console_ops = {
	.fo_name = "rvm-console",
	.fo_read = fbadop_read,
	.fo_write = console_write,
	.fo_ioctl = fbadop_ioctl,
	.fo_fcntl = fnullop_fcntl,
	.fo_poll = fnullop_poll,
	.fo_stat = console_stat,
	.fo_close = console_close,
	.fo_kqfilter = fnullop_kqfilter,
	.fo_restart = fnullop_restart,
};

int
rvm_console_stdio(void)
{
	for (int i = 0; i < 3; i++) {
		file_t *fp;
		int fd, error = fd_allocfile(&fp, &fd);
		if (error != 0) return error;
		if (fd != i) { fd_abort(curproc, fp, fd); return EBUSY; }
		fp->f_flag = FWRITE;
		fp->f_type = DTYPE_MISC;
		fp->f_ops = &console_ops;
		fd_affix(curproc, fp, fd);
	}
	return 0;
}
