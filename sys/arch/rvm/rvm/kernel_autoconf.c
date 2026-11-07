/* Minimal native bus: a single RVM CPU, with GeneralDriver console at boot. */
#include "opt_rvm.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/cpu.h>


static int mainbus_match(device_t, cfdata_t, void *);
static void mainbus_attach(device_t, device_t, void *);
static int cpu_match(device_t, cfdata_t, void *);
static void cpu_attach(device_t, device_t, void *);
void rvm_console_attach(void);
#ifdef RVM_EARLY_KERNEL
void rvm_kernel_smoke(void);
#endif

CFATTACH_DECL_NEW(mainbus, 0, mainbus_match, mainbus_attach, NULL, NULL);
CFATTACH_DECL_NEW(cpu, 0, cpu_match, cpu_attach, NULL, NULL);

static int
mainbus_match(device_t parent __unused, cfdata_t cf __unused, void *aux __unused)
{
	return 1;
}

static void
mainbus_attach(device_t parent __unused, device_t self, void *aux __unused)
{
	const char *name = "cpu";

	aprint_naive("\n");
	aprint_normal("\n");
	config_found(self, __UNCONST(name), NULL, CFARGS_NONE);
	name = "hardfile";
	config_found(self, __UNCONST(name), NULL, CFARGS_NONE);
}

static int
cpu_match(device_t parent __unused, cfdata_t cf __unused, void *aux)
{
	return strcmp(aux, "cpu") == 0;
}

static void
cpu_attach(device_t parent __unused, device_t self, void *aux __unused)
{
	curcpu()->ci_dev = self;
	aprint_naive(": RVM CPU\n");
	aprint_normal(": 64-bit RVM CPU, 64 KiB pages, uniprocessor\n");
}

void
cpu_configure(void)
{
	(void)splhigh();
	if (config_rootfound("mainbus", NULL) == NULL)
		panic("RVM mainbus not configured");
	rvm_console_attach();
	spl0();
}

void
cpu_rootconf(void)
{
#ifdef RVM_EARLY_KERNEL
	rvm_kernel_smoke();
#endif
	rootconf();
}

void
cpu_dumpconf(void)
{
	/* No dump-capable block device is attached yet. */
}
