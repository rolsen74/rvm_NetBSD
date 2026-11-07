/* Only images carrying the explicit test marker are ever written by this
 * EARLY check. Writes are verified, restored and flushed before returning.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/disk.h>
#include <sys/proc.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <dev/ldvar.h>

void rvm_disk_smoke(void);
extern struct cfdriver ld_cd;
extern const struct bdevsw ld_bdevsw;
#define CHECK(c) do { if (!(c)) panic("RVM disk check: " #c); } while (0)
#define TEST_MAGIC UINT64_C(0x52564d5445535431)

static int
transfer(dev_t dev, struct buf *bp, daddr_t block, size_t size, bool write)
{
	bp->b_dev = dev;
	bp->b_blkno = block;
	bp->b_bcount = size;
	bp->b_flags = write ? B_WRITE : B_READ;
	bp->b_error = 0;
	bp->b_resid = size;
	bp->b_oflags &= ~BO_DONE;
	ld_bdevsw.d_strategy(bp);
	return biowait(bp);
}

void
rvm_disk_smoke(void)
{
	struct ld_softc *ld = device_lookup_private(&ld_cd, 0);
	struct buf *bp;
	dev_t dev;
	unsigned char *bytes;
	int error;
	if (ld == NULL || !(ld->sc_flags & LDF_ENABLED)) {
		printf("RVM hardfile: absent device probe recovered safely\n");
		return;
	}
	dev = MAKEDISKDEV(bdevsw_lookup_major(&ld_bdevsw), 0, RAW_PART);
	CHECK(ld_bdevsw.d_open(dev, FREAD | FWRITE, S_IFBLK, curlwp) == 0);
	bp = geteblk(PAGE_SIZE);
	CHECK(transfer(dev, bp, ld->sc_secperunit - 1, DEV_BSIZE, false) == 0);
	if (*(uint64_t *)bp->b_data != TEST_MAGIC || ((uint64_t *)bp->b_data)[1] != 1 ||
	    ld->sc_secperunit < 1024) {
		brelse(bp, 0);
		CHECK(ld_bdevsw.d_close(dev, FREAD | FWRITE, S_IFBLK, curlwp) == 0);
		printf("RVM hardfile: disk readable; test marker absent, no writes performed\n");
		return;
	}
	CHECK(transfer(dev, bp, 128, PAGE_SIZE, false) == 0 && bp->b_resid == 0);
	bytes = bp->b_data;
	for (size_t i = 0; i < PAGE_SIZE; i++) CHECK(bytes[i] == ((i ^ 0x5a) & 255));
	memset(bytes, 0x6d, PAGE_SIZE);
	error = transfer(dev, bp, 128, PAGE_SIZE, true);
	CHECK(error == 0 || error == EROFS);
	CHECK(transfer(dev, bp, 128, PAGE_SIZE, false) == 0);
	for (size_t i = 0; i < PAGE_SIZE; i++)
		CHECK(bytes[i] == (error == EROFS ? ((i ^ 0x5a) & 255) : 0x6d));
	if (error == 0) {
		for (size_t i = 0; i < PAGE_SIZE; i++) bytes[i] = (i ^ 0x5a) & 255;
		CHECK(transfer(dev, bp, 128, PAGE_SIZE, true) == 0);
	}
	CHECK(ld_bdevsw.d_ioctl(dev, DIOCCACHESYNC, NULL, FWRITE, curlwp) == 0);
	CHECK(transfer(dev, bp, ld->sc_secperunit, DEV_BSIZE, false) == 0 && bp->b_resid == DEV_BSIZE);
	CHECK(transfer(dev, bp, 128, DEV_BSIZE + 1, false) == EINVAL);
	brelse(bp, 0);
	CHECK(ld_bdevsw.d_close(dev, FREAD | FWRITE, S_IFBLK, curlwp) == 0);
	printf("RVM hardfile: MI disk read/write, 64 KiB DMA, flush, bounds and %s verified\n",
	    error == EROFS ? "read-only policy" : "restoration");
}
