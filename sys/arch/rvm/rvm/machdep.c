

/*
 * NetBSD/RVM - early machine-dependent bootstrap.
 *
 * Initial standalone smoke test for the RVM GCC -> FHR -> VMRun path.
 * This is NOT the complete NetBSD machine-dependent implementation.
 *
 * Entry: locore.S calls rvm_early_init() in supervisor mode after
 * VMRun has loaded the FHR sections and established a valid SSP.
 *
 * TODO before NetBSD's kern/init_main.c:main() can run:
 *   - validate boot ABI and memory map
 *   - initialize the trap/interrupt vector base
 *   - bootstrap pmap, physical memory and the primary CPU
 *   - provide NetBSD's platform routines (consinit, cpu_startup, ...)
 */

/*
 * Observable with the VMRun debugger even without a console device.
 * 0: not entered, 1: entered, 2: bootstrap smoke test complete.
 * Volatile prevents these stores from being optimized away.
 */
volatile unsigned long rvm_boot_stage = 0;

/* Also visible in the FHR image / VMRun memory inspector. */
const char rvm_boot_banner[] = "NetBSD/RVM early bootstrap";

/*
 * Define RVM_EARLY_CONSOLE_HOOK only when you have implemented
 * rvm_early_putchar() using an actual VMRun debug/console interface.
 * The hook must work before NetBSD device and console initialization.
 */
#ifdef RVM_EARLY_CONSOLE_HOOK
extern void rvm_early_putchar ( int ch );

static void
rvm_early_puts ( const char * s )
{
	while ( *s != '\0' )
		rvm_early_putchar ( (unsigned char)*s++ );
}
#endif

void
rvm_early_init ( void )
{
	rvm_boot_stage = 1;

#ifdef RVM_EARLY_CONSOLE_HOOK
	rvm_early_puts ( rvm_boot_banner );
	rvm_early_putchar ( '\n' );
#endif

	rvm_boot_stage = 2;
	/* Return to locore.S, which loops if bootstrapping ends. */
}
