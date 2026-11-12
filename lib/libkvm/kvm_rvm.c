/*	$NetBSD$	*/

/*
 * RVM machine-dependent routines for libkvm.
 *
 * Live-kernel users obtain process and file information through sysctl and
 * need only the address bounds initialized here.  Crash-dump virtual address
 * translation remains unsupported until the RVM kcore format is defined.
 */

#include <sys/param.h>
#include <sys/types.h>

#include <uvm/uvm_extern.h>

#include <limits.h>
#include <kvm.h>

#include <machine/vmparam.h>

#include "kvm_private.h"

void
_kvm_freevtop(kvm_t *kd)
{
	(void)kd;
}

int
_kvm_initvtop(kvm_t *kd)
{
	_kvm_err(kd, 0, "RVM crash-dump translation is not implemented");
	return -1;
}

int
_kvm_kvatop(kvm_t *kd, vaddr_t va, paddr_t *pa)
{
	(void)va;
	(void)pa;
	_kvm_err(kd, 0, "RVM crash-dump translation is not implemented");
	return 0;
}

off_t
_kvm_pa2off(kvm_t *kd, paddr_t pa)
{
	(void)pa;
	_kvm_err(kd, 0, "RVM crash-dump translation is not implemented");
	return (off_t)-1;
}

int
_kvm_mdopen(kvm_t *kd)
{
	kd->min_uva = VM_MIN_ADDRESS;
	kd->max_uva = VM_MAXUSER_ADDRESS;
	return 0;
}
