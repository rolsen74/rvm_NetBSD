/* Native, static FHR executables.  Parse and relocate a bounded snapshot
 * before exec replaces the old address space.  FHR has no entry field:
 * the first code section starts with the executable's entry routine.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/kmem.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/vnode.h>
#include <sys/endian.h>
#include <uvm/uvm_extern.h>

#define FHR_NSECT 32
#define FHR_MAXFILE (16 * 1024 * 1024)
#define FHR_MAXMEM (64 * 1024 * 1024)

struct fhr_section {
	vaddr_t addr;
	size_t size;
	unsigned prot;
	uint8_t *data;
};
struct fhr_image {
	uint8_t *bytes;
	size_t length;
	struct fhr_section section[FHR_NSECT];
};

static void
fhr_free(void *arg)
{
	struct fhr_image *image = arg;
	kmem_free(image->bytes, image->length);
	kmem_free(image, sizeof(*image));
}

/* CRC-16, polynomial x^16+x^15+x^2+1, MSB first, initial 0xffff. */
static uint16_t
fhr_crc(const uint8_t *p, size_t length)
{
	uint16_t crc = 0xffff;
	while (length-- != 0) {
		crc ^= (uint16_t)*p++ << 8;
		for (unsigned bit = 0; bit < 8; bit++)
			crc = (crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0);
	}
	return crc;
}

static bool
fhr_integer(uint8_t **cursor, const uint8_t *end, uint64_t *value)
{
	uint8_t mask;
	if (*cursor == end)
		return false;
	mask = *(*cursor)++;
	*value = 0;
	if (mask < 0x80) {
		*value = mask;
		return true;
	}
	for (unsigned bit = 0; bit < 8; bit++) {
		*value <<= 8;
		if (mask & (1U << bit)) {
			if (*cursor == end)
				return false;
			*value |= *(*cursor)++;
		}
	}
	return true;
}

static int
fhr_parse(struct fhr_image *image, unsigned n)
{
	uint8_t *bytes = image->bytes, *p, *end = bytes + image->length;
	vaddr_t address = 0x400000;
	size_t memory = 0;
	if (image->length < 16 + 12 * n)
		return ENOEXEC;
	for (unsigned i = 0; i < n; i++) {
		const uint8_t *info = bytes + 16 + i * 12;
		struct fhr_section *s = &image->section[i];
		uint64_t size = be64dec(info);
		unsigned align = be16dec(info + 8), flags = be16dec(info + 10);
		if (size == 0 || size > FHR_MAXMEM - memory || align > 16 ||
		    (flags & ~7U) || !(flags & 6) || (flags & 5) == 5)
			return ENOEXEC;
		s->addr = address;
		s->size = size;
		s->prot = VM_PROT_READ | ((flags & 1) ? VM_PROT_WRITE : 0) |
		    ((flags & 4) ? VM_PROT_EXECUTE : 0);
		memory += round_page(size);
		if (memory > FHR_MAXMEM)
			return ENOEXEC;
		address += round_page(size);
	}
	if (!(image->section[0].prot & VM_PROT_EXECUTE))
		return ENOEXEC;
	p = bytes + 16 + 12 * n;
	for (unsigned i = 0; i < n; i++) {
		struct fhr_section *s = &image->section[i];
		uint32_t tag;
		if (end - p < 4)
			return ENOEXEC;
		tag = be32dec(p);
		p += 4;
		if (tag == 0x42535300) {
			if (s->prot & VM_PROT_EXECUTE)
				return ENOEXEC;
		} else {
			uint64_t length;
			uint8_t *limit, *rel;
			if (tag != ((s->prot & VM_PROT_EXECUTE) ?
			    0x434f4400U : 0x44415400U) || end - p < 10)
				return ENOEXEC;
			length = be64dec(p);
			p += 8;
			if (length < s->size + 3 || length > (uint64_t)(end - p))
				return ENOEXEC;
			limit = p + length;
			if (be16dec(p) != fhr_crc(p + 2, length - 2))
				return ENOEXEC;
			s->data = p + 2;
			rel = s->data + s->size;
			for (;;) {
				uint64_t count, target, kind;
				if (!fhr_integer(&rel, limit, &count))
					return ENOEXEC;
				if (count == 0)
					break;
				if (count > (uint64_t)(limit - rel) ||
				    !fhr_integer(&rel, limit, &target) || target >= n ||
				    !fhr_integer(&rel, limit, &kind) || kind != 0)
					return ENOEXEC;
				while (count-- != 0) {
					uint64_t offset, value;
					if (!fhr_integer(&rel, limit, &offset) ||
					    s->size < 8 || offset > s->size - 8)
						return ENOEXEC;
					value = be64dec(s->data + offset);
					if (value > UINT64_MAX - image->section[target].addr)
						return ENOEXEC;
					be64enc(s->data + offset,
					    value + image->section[target].addr);
				}
			}
			if (rel != limit)
				return ENOEXEC;
			p = limit;
		}
		if (end - p < 4 || be32dec(p) != 0x454e4400)
			return ENOEXEC;
		p += 4;
	}
	return p == end ? 0 : ENOEXEC;
}

static int
fhr_map(struct lwp *l, struct exec_vmcmd *cmd)
{
	struct fhr_section *s = (void *)(uintptr_t)cmd->ev_offset;
	struct vm_map *map = &l->l_proc->p_vmspace->vm_map;
	vaddr_t addr = cmd->ev_addr, end = addr + round_page(cmd->ev_len);
	int error = uvm_map(map, &addr, end - addr, NULL, UVM_UNKNOWN_OFFSET, 0,
	    UVM_MAPFLAG(UVM_PROT_RW, UVM_PROT_ALL, UVM_INH_COPY,
	    UVM_ADV_NORMAL, UVM_FLAG_FIXED|UVM_FLAG_COPYONW));
	if (error == 0 && s->data != NULL)
		error = copyout(s->data, (void *)addr, s->size);
	if (error == 0)
		error = uvm_map_protect(map, addr, end, cmd->ev_prot, true);
	if (error == 0)
		error = uvm_map_protect(map, addr, end, cmd->ev_prot, false);
	return error;
}

static int
fhr_copyargs(struct lwp *l, struct exec_package *epp,
    struct ps_strings *ps, char **stack, void *arg)
{
	int error = copyargs(l, epp, ps, stack, arg);
	if (error == 0)
		exec_free_emul_arg(epp);
	return error;
}

static int
fhr_makecmds(struct lwp *l, struct exec_package *epp)
{
	const uint8_t *h = epp->ep_hdr;
	struct fhr_image *image;
	unsigned n;
	int error;
	if (epp->ep_hdrvalid < 16 || be32dec(h) != 0x46485200 ||
	    h[4] != 1 || h[5] != 1 || h[6] != 0 || h[7] != 0 ||
	    be16dec(h + 8) != 0 || be16dec(h + 10) != 0x100 ||
	    be16dec(h + 12) != 0x100 ||
	    (n = be16dec(h + 14)) == 0 || n > FHR_NSECT ||
	    epp->ep_vap->va_size < 16 || epp->ep_vap->va_size > FHR_MAXFILE)
		return ENOEXEC;
	error = vn_marktext(epp->ep_vp);
	if (error != 0)
		return error;
	image = kmem_zalloc(sizeof(*image), KM_SLEEP);
	image->length = epp->ep_vap->va_size;
	image->bytes = kmem_alloc(image->length, KM_SLEEP);
	error = exec_read(l, epp->ep_vp, 0, image->bytes, image->length, 0);
	if (error == 0 && memcmp(h, image->bytes, 16) != 0)
		error = ENOEXEC;
	if (error == 0)
		error = fhr_parse(image, n);
	if (error != 0) {
		fhr_free(image);
		return error;
	}
	epp->ep_emul_arg = image;
	epp->ep_emul_arg_free = fhr_free;
	epp->ep_entry = image->section[0].addr;
	epp->ep_taddr = epp->ep_entry;
	epp->ep_tsize = 0;
	epp->ep_daddr = image->section[n - 1].addr +
	    round_page(image->section[n - 1].size);
	epp->ep_dsize = 0;
	for (unsigned i = 0; i < n; i++) {
		struct fhr_section *s = &image->section[i];
		if (s->prot & VM_PROT_EXECUTE)
			epp->ep_tsize = s->addr + round_page(s->size) - epp->ep_taddr;
		else if (s->addr < epp->ep_daddr)
			epp->ep_daddr = s->addr;
		NEW_VMCMD2(&epp->ep_vmcmds, fhr_map, s->size, s->addr, NULL,
		    (uintptr_t)s, s->prot, VMCMD_FIXED);
	}
	epp->ep_dsize = image->section[n - 1].addr +
	    round_page(image->section[n - 1].size) - epp->ep_daddr;
	return exec_setup_stack(l, epp);
}

MODULE(MODULE_CLASS_EXEC, exec_fhr, NULL);
/* MI calls this unconditionally for a core-producing signal. FHR has no
 * core format yet; a missing callback must not turn a user crash into a
 * kernel call through NULL. */
static int
fhr_coredump(struct lwp *l __unused, struct coredump_iostate *io __unused)
{
	return EOPNOTSUPP;
}
static struct execsw exec_fhr_execsw = {
	.es_hdrsz = 16,
	.es_makecmds = fhr_makecmds,
	.es_emul = &emul_netbsd,
	.es_prio = EXECSW_PRIO_ANY,
	.es_copyargs = fhr_copyargs,
	.es_coredump = fhr_coredump,
	.es_setregs = setregs,
	.es_setup_stack = exec_setup_stack,
};
static int
exec_fhr_modcmd(modcmd_t cmd, void *arg __unused)
{
	switch (cmd) {
	case MODULE_CMD_INIT: return exec_add(&exec_fhr_execsw, 1);
	case MODULE_CMD_FINI: return exec_remove(&exec_fhr_execsw, 1);
	case MODULE_CMD_AUTOUNLOAD: return EBUSY;
	default: return ENOTTY;
	}
}
