/* HARDFILE v1 backend for MI ld/dk. Synchronous MMIO, serialized commands and
 * one physically contiguous 64 KiB bounce page. Completion uses an MI softint
 * after ld has accounted for the queued request. Hardware IRQs remain disabled.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/bufq.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <uvm/uvm.h>
#include <dev/ldvar.h>
#include <machine/hardfile.h>
#include <machine/mmio.h>

struct ld_rvm_softc {
	struct ld_softc sc_ld; /* ld requires this to be first. */
	vaddr_t sc_kva;
	volatile uint64_t *sc_io;
	struct vm_page *sc_page;
	paddr_t sc_pa;
	void *sc_softint;
	struct buf *sc_bp;
	bool sc_readonly;
};
static char rvm_disk_name[] = "RVM hardfile";
#define REG(sc, r) ((sc)->sc_io[(r) / sizeof(uint64_t)])

static int
ld_rvm_match(device_t parent __unused, cfdata_t cf __unused, void *aux)
{
	uint64_t id, abi;
	vaddr_t va;
	int found;
	if (strcmp(aux, "hardfile") != 0) return 0;
	/* GeneralDriver and HARDFILE share the wired MMIO identity page. */
	va = RVM_HF_DEFAULT_ADDRESS;
	if (va == 0) return 0;
	found = rvm_mmio_peek8((void *)va, &id) == 0 && id == RVM_HF_MAGIC &&
	    rvm_mmio_peek8((void *)(va + RVM_HF_ABI), &abi) == 0 && abi == RVM_HF_VERSION;
	return found;
}
static int
command(struct ld_rvm_softc *sc, uint64_t cmd)
{
	uint64_t status, error;
	__asm volatile("" ::: "memory");
	REG(sc, RVM_HF_COMMAND) = cmd;
	__asm volatile("" ::: "memory");
	status = REG(sc, RVM_HF_STATUS);
	error = REG(sc, RVM_HF_ERROR);
	REG(sc, RVM_HF_IRQ_STATUS) = 1;
	if (!(status & RVM_HF_S_DONE) || (status & RVM_HF_S_BUSY)) return EIO;
	if (error == RVM_HF_E_READONLY) return EROFS;
	if (error != 0 || (status & RVM_HF_S_ERROR)) return EIO;
	return 0;
}
static int
ld_rvm_start(struct ld_softc *ld, struct buf *bp)
{
	struct ld_rvm_softc *sc = (void *)ld;
	KASSERT(mutex_owned(&ld->sc_mutex));
	KASSERT(sc->sc_bp == NULL);
	if (sc->sc_readonly && !(bp->b_flags & B_READ)) return EROFS;
	if (bp->b_rawblkno < 0 || bp->b_bcount <= 0 || bp->b_bcount > PAGE_SIZE ||
	    bp->b_bcount % RVM_HF_SECTOR_SIZE ||
	    (uint64_t)bp->b_rawblkno >= ld->sc_secperunit ||
	    bp->b_bcount / RVM_HF_SECTOR_SIZE > ld->sc_secperunit - bp->b_rawblkno) return EINVAL;
	sc->sc_bp = bp;
	softint_schedule(sc->sc_softint);
	return 0;
}
static void
ld_rvm_complete(void *arg)
{
	struct ld_rvm_softc *sc = arg;
	struct ld_softc *ld = &sc->sc_ld;
	struct buf *bp;
	void *bounce = (void *)(uintptr_t)sc->sc_pa;
	int error;
	mutex_enter(&ld->sc_mutex);
	bp = sc->sc_bp;
	KASSERT(bp != NULL);
	if (!(bp->b_flags & B_READ)) memcpy(bounce, bp->b_data, bp->b_bcount);
	REG(sc, RVM_HF_LBA) = bp->b_rawblkno;
	REG(sc, RVM_HF_COUNT) = bp->b_bcount / RVM_HF_SECTOR_SIZE;
	REG(sc, RVM_HF_BUFFER) = sc->sc_pa;
	error = command(sc, (bp->b_flags & B_READ) ? RVM_HF_CMD_READ : RVM_HF_CMD_WRITE);
	if (error == 0 && REG(sc, RVM_HF_TRANSFERRED) != bp->b_bcount) error = EIO;
	if (error == 0 && (bp->b_flags & B_READ)) memcpy(bp->b_data, bounce, bp->b_bcount);
	bp->b_error = error;
	bp->b_resid = error ? bp->b_bcount : 0;
	sc->sc_bp = NULL;
	mutex_exit(&ld->sc_mutex);
	lddone(ld, bp);
}
static int
ld_rvm_ioctl(struct ld_softc *ld, u_long cmd, void *data __unused,
    int32_t flags __unused, bool poll __unused)
{
	struct ld_rvm_softc *sc = (void *)ld;
	int error;
	if (cmd != DIOCCACHESYNC) return EPASSTHROUGH;
	mutex_enter(&ld->sc_mutex);
	error = sc->sc_bp != NULL ? EBUSY : command(sc, RVM_HF_CMD_FLUSH);
	mutex_exit(&ld->sc_mutex);
	return error;
}
static int
ld_rvm_dump(struct ld_softc *ld __unused, void *data __unused,
    daddr_t blk __unused, int count __unused)
{
	return EOPNOTSUPP;
}
static void
ld_rvm_attach(device_t parent __unused, device_t self, void *aux __unused)
{
	struct ld_rvm_softc *sc = device_private(self);
	struct ld_softc *ld = &sc->sc_ld;
	ld->sc_dv = self;
	sc->sc_kva = RVM_HF_DEFAULT_ADDRESS;
	if (sc->sc_kva == 0) { aprint_error(": no register VA\n"); return; }
	sc->sc_io = (void *)sc->sc_kva;
	if (REG(sc, RVM_HF_SECTOR_BYTES) != DEV_BSIZE ||
	    REG(sc, RVM_HF_SECTORS) == 0 || REG(sc, RVM_HF_SECTORS) > INT64_MAX / DEV_BSIZE ||
	    REG(sc, RVM_HF_MAX_BYTES) < DEV_BSIZE) goto failed;
	sc->sc_page = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO);
	if (sc->sc_page == NULL) goto failed;
	sc->sc_page->flags &= ~PG_BUSY;
	UVM_PAGE_OWN(sc->sc_page, NULL);
	sc->sc_pa = VM_PAGE_TO_PHYS(sc->sc_page);
	sc->sc_softint = softint_establish(SOFTINT_BIO | SOFTINT_MPSAFE, ld_rvm_complete, sc);
	if (sc->sc_softint == NULL) { uvm_pagefree(sc->sc_page); goto failed; }
	sc->sc_readonly = !(REG(sc, RVM_HF_FEATURES) & RVM_HF_F_WRITABLE);
	REG(sc, RVM_HF_IRQ_ENABLE) = 0;
	REG(sc, RVM_HF_IRQ_STATUS) = 1;
	ld->sc_flags = LDF_ENABLED | LDF_MPSAFE | LDF_NO_RND;
	ld->sc_maxxfer = MIN(MIN(PAGE_SIZE, MAXPHYS), REG(sc, RVM_HF_MAX_BYTES));
	ld->sc_secsize = DEV_BSIZE;
	ld->sc_secperunit = REG(sc, RVM_HF_SECTORS);
	ld->sc_maxqueuecnt = 1;
	ld->sc_typename = rvm_disk_name;
	ld->sc_start = ld_rvm_start;
	ld->sc_dump = ld_rvm_dump;
	ld->sc_ioctl = ld_rvm_ioctl;
	aprint_normal(": RVM HARDFILE v1, %s, polled MMIO\n", sc->sc_readonly ? "read-only" : "read/write");
	ldattach(ld, BUFQ_DISK_DEFAULT_STRAT);
	return;
failed:
	sc->sc_kva = 0;
	aprint_error(": unsupported device or insufficient resources\n");
}
CFATTACH_DECL_NEW(ld_rvm, sizeof(struct ld_rvm_softc), ld_rvm_match, ld_rvm_attach, NULL, NULL);
