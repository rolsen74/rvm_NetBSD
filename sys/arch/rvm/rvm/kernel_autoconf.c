/* Native bus: CPU attachment and GeneralDriver console at boot. */
#include "opt_rvm.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/cpu.h>
#include <machine/irq.h>


static int mainbus_match(device_t, cfdata_t, void *);
static void mainbus_attach(device_t, device_t, void *);
static int cpu_match(device_t, cfdata_t, void *);
static void cpu_attach(device_t, device_t, void *);
void rvm_console_attach(void);
void rvm_bpf_init(void);
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
#ifdef MULTIPROCESSOR
	/* Each VMEmu CPU publishes its slot before probing the next one. */
	for (unsigned int id = 1; id < MAXCPUS; id++) {
		for (unsigned int i = 0; i < 1000 &&
		    rvm_secondary_arrived[id] == 0; i++)
			delay(100);
		if (rvm_secondary_arrived[id] == 0)
			break;
		config_found(self, __UNCONST(name), NULL, CFARGS_NONE);
		rvm_cpu_count = id + 1;
	}
#endif
	name = "hardfile";
	config_found(self, __UNCONST(name), NULL, CFARGS_NONE);
	name = "entropy";
	config_found(self, __UNCONST(name), NULL, CFARGS_NONE);
	name = "network";
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
	struct cpu_info *ci = &cpu_info_primary;
#ifdef MULTIPROCESSOR
	if (device_unit(self) > 0) {
		unsigned int id = device_unit(self);
		KASSERT(id < MAXCPUS);
		ci = &cpu_info_secondary[id - 1];
		ci->ci_cpuid = id;
		ci->ci_cpl = IPL_HIGH;
		int error = mi_cpu_attach(ci);
		if (error != 0) panic("RVM cpu%u attach failed: %d", id, error);
	}
#endif
	ci->ci_dev = self;
	aprint_naive(": RVM CPU\n");
	aprint_normal(": 64-bit RVM CPU, 64 KiB pages\n");
}

void
cpu_configure(void)
{
	rvm_bpf_init();
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
