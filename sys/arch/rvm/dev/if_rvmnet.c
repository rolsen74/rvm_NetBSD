/* VMEmu TAP Ethernet. Commands and DMA are synchronous; RX is polled. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/callout.h>
#include <sys/mbuf.h>
#include <uvm/uvm.h>
#include <net/if.h>
#include <net/if_ether.h>
#include <net/bpf.h>
#include <machine/network.h>
#include <machine/mmio.h>
#include <machine/asyncirq.h>

struct rvmnet_softc {
	struct ethercom sc_ec;
	callout_t sc_poll;
	struct vm_page *sc_page;
	paddr_t sc_pa;
	volatile uint64_t *sc_io;
};

#define REG(sc, r) ((sc)->sc_io[(r) / sizeof(uint64_t)])

static int rvmnet_match(device_t, cfdata_t, void *);
static void rvmnet_attach(device_t, device_t, void *);
static void rvmnet_poll(void *);
static void rvmnet_start(struct ifnet *);
static int rvmnet_ioctl(struct ifnet *, u_long, void *);
static int rvmnet_init(struct ifnet *);
static void rvmnet_stop(struct ifnet *, int);

CFATTACH_DECL_NEW(rvmnet, sizeof(struct rvmnet_softc), rvmnet_match,
    rvmnet_attach, NULL, NULL);

static int
rvmnet_match(device_t parent __unused, cfdata_t cf __unused, void *aux)
{
	uint64_t id, abi;
	if (strcmp(aux, "network") != 0)
		return 0;
	return rvm_mmio_peek8((void *)RVM_NET_ADDRESS, &id) == 0 &&
	    id == RVM_NET_MAGIC &&
	    rvm_mmio_peek8((void *)(RVM_NET_ADDRESS + RVM_NET_ABI), &abi) == 0 &&
	    abi == RVM_NET_VERSION;
}

static void
rvmnet_attach(device_t parent __unused, device_t self, void *aux __unused)
{
	struct rvmnet_softc *sc = device_private(self);
	struct ifnet *ifp = &sc->sc_ec.ec_if;
	uint8_t mac[ETHER_ADDR_LEN];
	uint64_t address;

	sc->sc_io = (void *)RVM_NET_ADDRESS;
	if ((REG(sc, RVM_NET_FEATURES) & RVM_NET_F_TAP) == 0)
		goto failed;
	sc->sc_page = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO);
	if (sc->sc_page == NULL)
		goto failed;
	sc->sc_page->flags &= ~PG_BUSY;
	UVM_PAGE_OWN(sc->sc_page, NULL);
	sc->sc_pa = VM_PAGE_TO_PHYS(sc->sc_page);
	REG(sc, RVM_NET_BUFFER) = sc->sc_pa;
	address = REG(sc, RVM_NET_MAC);
	for (unsigned i = 0; i < ETHER_ADDR_LEN; i++)
		mac[i] = address >> (8 * (ETHER_ADDR_LEN - 1 - i));

	callout_init(&sc->sc_poll, CALLOUT_MPSAFE);
	if_initname(ifp, "rvmnet", device_unit(self));
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
#ifdef NET_MPSAFE
	ifp->if_extflags = IFEF_MPSAFE;
#endif
	ifp->if_start = rvmnet_start;
	ifp->if_ioctl = rvmnet_ioctl;
	ifp->if_init = rvmnet_init;
	ifp->if_stop = rvmnet_stop;
	IFQ_SET_MAXLEN(&ifp->if_snd, IFQ_MAXLEN);
	IFQ_SET_READY(&ifp->if_snd);
	if_initialize(ifp);
	ifp->if_percpuq = if_percpuq_create(ifp);
	ether_ifattach(ifp, mac);
	if_register(ifp);
	if_link_state_change(ifp, LINK_STATE_DOWN);
	aprint_normal(": VMEmu TAP Ethernet, address %s\n", ether_sprintf(mac));
	return;
failed:
	aprint_error(": unsupported device or no DMA page\n");
}

static int
rvmnet_init(struct ifnet *ifp)
{
	struct rvmnet_softc *sc = ifp->if_softc;
	ifp->if_flags |= IFF_RUNNING;
	if_link_state_change(ifp, LINK_STATE_UP);
	callout_reset(&sc->sc_poll, 1, rvmnet_poll, sc);
	rvmnet_start(ifp);
	return 0;
}

static void
rvmnet_stop(struct ifnet *ifp, int disable __unused)
{
	struct rvmnet_softc *sc = ifp->if_softc;
	ifp->if_flags &= ~IFF_RUNNING;
	callout_stop(&sc->sc_poll);
	if_link_state_change(ifp, LINK_STATE_DOWN);
}

static int
rvmnet_ioctl(struct ifnet *ifp, u_long cmd, void *data)
{
	if (cmd == SIOCADDMULTI || cmd == SIOCDELMULTI)
		return 0;
	return ether_ioctl(ifp, cmd, data);
}

static void
rvmnet_start(struct ifnet *ifp)
{
	struct rvmnet_softc *sc = ifp->if_softc;
	struct mbuf *m;
	uint8_t *bounce = (void *)(uintptr_t)sc->sc_pa;
	uint64_t status;
	unsigned int guard;
	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;
	for (;;) {
		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		if (m->m_pkthdr.len < ETHER_HDR_LEN ||
		    m->m_pkthdr.len > RVM_NET_MAX_FRAME) {
			if_statinc(ifp, if_oerrors);
			m_freem(m);
			continue;
		}
		guard = rvm_intr_suspend();
		m_copydata(m, 0, m->m_pkthdr.len, bounce);
		REG(sc, RVM_NET_LENGTH) = m->m_pkthdr.len;
		__asm volatile("" ::: "memory");
		REG(sc, RVM_NET_COMMAND) = RVM_NET_CMD_TX;
		status = REG(sc, RVM_NET_STATUS);
		rvm_intr_resume(guard);
		if (status & RVM_NET_S_ERROR)
			if_statinc(ifp, if_oerrors);
		else {
			bpf_mtap(ifp, m, BPF_D_OUT);
			if_statadd2(ifp, if_opackets, 1, if_obytes, m->m_pkthdr.len);
		}
		m_freem(m);
	}
}

static void
rvmnet_poll(void *arg)
{
	struct rvmnet_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ec.ec_if;
	uint8_t *bounce = (void *)(uintptr_t)sc->sc_pa;
	uint8_t frame[RVM_NET_MAX_FRAME];
	struct mbuf *m;
	uint64_t status, length;
	unsigned int guard;
	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;
	for (unsigned i = 0; i < 16; i++) {
		guard = rvm_intr_suspend();
		REG(sc, RVM_NET_COMMAND) = RVM_NET_CMD_RX;
		status = REG(sc, RVM_NET_STATUS);
		if (status & RVM_NET_S_EMPTY) {
			rvm_intr_resume(guard);
			break;
		}
		length = REG(sc, RVM_NET_LENGTH);
		if ((status & RVM_NET_S_ERROR) || length < ETHER_HDR_LEN ||
		    length > RVM_NET_MAX_FRAME || length > MCLBYTES) {
			rvm_intr_resume(guard);
			if_statinc(ifp, if_ierrors);
			break;
		}
		__asm volatile("" ::: "memory");
		memcpy(frame, bounce, length);
		rvm_intr_resume(guard);
		MGETHDR(m, M_DONTWAIT, MT_DATA);
		if (m == NULL) {
			if_statinc(ifp, if_ierrors);
			break;
		}
		MCLAIM(m, &sc->sc_ec.ec_rx_mowner);
		if (length > MHLEN) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_freem(m);
				if_statinc(ifp, if_ierrors);
				break;
			}
		}
		memcpy(mtod(m, void *), frame, length);
		m->m_len = m->m_pkthdr.len = length;
		m_set_rcvif(m, ifp);
		if_statadd2(ifp, if_ipackets, 1, if_ibytes, length);
		bpf_mtap(ifp, m, BPF_D_IN);
		if_percpuq_enqueue(ifp->if_percpuq, m);
	}
	callout_reset(&sc->sc_poll, 1, rvmnet_poll, sc);
}
