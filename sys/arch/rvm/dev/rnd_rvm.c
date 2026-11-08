/* GeneralDriver host CSPRNG source. Credit only successfully returned words. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/rndsource.h>
#include <machine/generic.h>

struct rvmrnd_softc {
	krndsource_t source;
	bool busy;
};
static int rvmrnd_match(device_t, cfdata_t, void *);
static void rvmrnd_attach(device_t, device_t, void *);
static void rvmrnd_get(size_t, void *);
static volatile struct rvm_general_driver * const gd = (void *)RVM_GENERIC_ADDR;
CFATTACH_DECL_NEW(rvmrnd, sizeof(struct rvmrnd_softc), rvmrnd_match,
    rvmrnd_attach, NULL, NULL);

static int
rvmrnd_match(device_t parent __unused, cfdata_t cf __unused, void *aux)
{
	return strcmp(aux, "entropy") == 0 && (gd->entropy_status & 1);
}

static void
rvmrnd_get(size_t wanted, void *arg)
{
	struct rvmrnd_softc *sc = arg;
	uint64_t data[16];
	size_t count = 0, words = MIN(howmany(MIN(wanted, sizeof(data)), sizeof(data[0])), __arraycount(data));
	int s = splhigh();
	if (sc->busy) { splx(s); return; }
	sc->busy = true;
	while (count < words) {
		uint64_t word = gd->entropy_data;
		if ((gd->entropy_status & 3) != 3) break;
		data[count++] = word;
	}
	splx(s);
	if (count) rnd_add_data_sync(&sc->source, data, count * sizeof(data[0]),
	    count * sizeof(data[0]) * NBBY);
	explicit_memset(data, 0, sizeof(data));
	s = splhigh(); sc->busy = false; splx(s);
}

static void
rvmrnd_attach(device_t parent __unused, device_t self, void *aux __unused)
{
	struct rvmrnd_softc *sc = device_private(self);
	aprint_normal(": GeneralDriver host random source\n");
	rndsource_setcb(&sc->source, rvmrnd_get, sc);
	rnd_attach_source(&sc->source, device_xname(self), RND_TYPE_RNG,
	    RND_FLAG_COLLECT_VALUE | RND_FLAG_HASCB);
	rvmrnd_get(64, sc);
}
