/* Native NetBSD disklabel geometry; a disk driver is not attached yet. */
#ifndef _RVM_DISKLABEL_H_
#define _RVM_DISKLABEL_H_

#define LABELSECTOR 1
#define LABELOFFSET 0
#define MAXPARTITIONS 16
#define RAW_PART 2
#define LABELUSESMBR 0

struct cpu_disklabel {
	int cd_unused;
};

#endif
