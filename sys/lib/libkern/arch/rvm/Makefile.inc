# RVM uses the MI C implementations of libkern routines.
# Compiler arithmetic support is selected by Makefile.compiler-rt.
SRCS+=rvm_bzero.c ffs.c
DPSRCS+=${KERNDIR}/../../arch/rvm/stand/kernel_cc.py
