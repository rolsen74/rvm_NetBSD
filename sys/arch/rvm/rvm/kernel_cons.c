/* GeneralDriver output console, available before normal autoconfiguration. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <machine/console.h>
#include <dev/cons.h>
#include <machine/boot.h>
#include <machine/generic.h>
extern const struct cdevsw rvmcons_cdevsw;

static void
rvm_cnputc(dev_t dev __unused, int ch)
{
	rvm_early_putchar(ch);
}

/* GeneralDriver supplies ASCII key values and a key-down bit at bit 16.
 * Poll until a character is available, as required by cngetc(9).
 */
static int
rvm_cngetc(dev_t dev __unused)
{
	volatile struct rvm_general_driver *gd = (void *)RVM_GENERIC_ADDR;
	for (;;) {
		uint64_t key = gd->keyboard;
		if (key != UINT64_MAX && (key & (1UL << 16)) &&
		    (key & 0xffff) < 128)
			return key & 0x7f;
		delay(1000);
	}
}

static void
rvm_cnpollc(dev_t dev __unused, int on)
{
	rvm_console_poll(on != 0);
}

static struct consdev rvm_cons = {
	.cn_getc = rvm_cngetc,
	.cn_putc = rvm_cnputc,
	.cn_pollc = rvm_cnpollc,
	.cn_dev = NODEV,
	.cn_pri = CN_INTERNAL,
};
struct consdev constab[] = { { .cn_pri = CN_DEAD } };

void
rvm_console_attach(void)
{
	int major = cdevsw_lookup_major(&rvmcons_cdevsw);
	KASSERT(major >= 0);
	rvm_cons.cn_dev = makedev(major, 0);
}

void
consinit(void)
{
	static bool initialized;

	if (!initialized) {
		rvm_early_consinit();
		cn_set_tab(&rvm_cons);
		initialized = true;
	} else if (rvm_boot_stage == 1) {
		rvm_boot_stage = 2;
		rvm_early_puts("NetBSD main(): console initialized\n");
	}
}
