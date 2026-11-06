/* MI masked delta/generation publication across narrow and 32-bit wraps. */
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <machine/boottime.h>
const char wrap_bad[] = "MI timecounter wrap regression at line 0x";
const char wrap_ok[] = "RVM MI timecounter narrow/32-bit wraps and timehands rotation verified.\n";
const char narrow_name[] = "test-narrow";
const char wide_name[] = "test-wide";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(wrap_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct timecounter narrow __attribute__((section(".data"))) = { 0 };
static struct timecounter wide __attribute__((section(".data"))) = { 0 };
static u_int counters[2] __attribute__((section(".data"))) = { 0 };
static u_int read_counter(struct timecounter *tc) { return *(u_int *)tc->tc_priv & tc->tc_counter_mask; }
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int round, n;
	struct timespec before, after;
	struct bintime live, cached;
	struct clockframe cf = { 0, 0, 1 };
	int64_t delta;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 28 && rvm_timecounter_init());
	for (round = 0; round < 2; round++) {
		struct timecounter *tc = round == 0 ? &narrow : &wide;
		binuptime(&live);
		counters[round] = round == 0 ? 250 : 0xfffffff8U;
		tc->tc_priv = &counters[round];
		tc->tc_get_timecount = read_counter;
		tc->tc_counter_mask = round == 0 ? 255 : UINT_MAX;
		tc->tc_frequency = 50; tc->tc_quality = round == 0 ? 200 : 300;
		tc->tc_name = round == 0 ? narrow_name : wide_name;
		tc_init(tc); getbinuptime(&cached);
		REQUIRE(bintimecmp(&live, &cached, ==) && timecounter == tc);
		for (n = 0; n < (round == 0 ? 300U : 32U); n++) {
			nanouptime(&before); counters[round]++;
			curcpu()->ci_intr_depth = 1; splhigh(); hardclock(&cf);
			curcpu()->ci_intr_depth = 0; spl0();
			nanouptime(&after); binuptime(&live); getbinuptime(&cached);
			delta = after.tv_nsec - before.tv_nsec;
			if (after.tv_sec == before.tv_sec + 1) delta += 1000000000;
			else REQUIRE(after.tv_sec == before.tv_sec);
			REQUIRE(delta >= 19999999 && delta <= 20000001);
			REQUIRE(bintimecmp(&live, &cached, ==) && live.sec == time_uptime);
		}
	}
	REQUIRE(time_uptime >= 7 && curlwp->l_tcgen == 0 && !cpu_intr_p());
	rvm_early_puts(wrap_ok);
}
