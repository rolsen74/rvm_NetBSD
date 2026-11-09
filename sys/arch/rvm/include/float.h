/* RVM uses IEEE binary32 and binary64; long double is also binary64. */
#ifndef _RVM_FLOAT_H_
#define _RVM_FLOAT_H_
#define LDBL_MANT_DIG __LDBL_MANT_DIG__
#define LDBL_DIG __LDBL_DIG__
#define LDBL_MIN_EXP __LDBL_MIN_EXP__
#define LDBL_MIN_10_EXP __LDBL_MIN_10_EXP__
#define LDBL_MAX_EXP __LDBL_MAX_EXP__
#define LDBL_MAX_10_EXP __LDBL_MAX_10_EXP__
#define LDBL_EPSILON __LDBL_EPSILON__
#define LDBL_MIN __LDBL_MIN__
#define LDBL_MAX __LDBL_MAX__
#include <sys/float_ieee754.h>
#endif
