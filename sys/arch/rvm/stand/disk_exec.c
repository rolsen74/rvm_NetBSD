/* Compiled user code: data-pointer relocations, writable zero storage,
 * native syscall bridge, and a return status observed by the parent PID 1.
 */
#include <sys/syscall.h>
extern long rvm_call(long, long, long, long, long);
extern long rvm_mount(long, long, long, long, long, long);
char disk_message[] = "NetBSD/RVM: GCC user executable, data relocations and execve verified\n";
char *disk_pointer = disk_message;
unsigned char disk_zero[131072] __attribute__((aligned(65536)));
char disk_path[] = "/etc/roundtrip";
char disk_payload[] = "RVM FFS write/read roundtrip\n";
char disk_readback[sizeof(disk_payload)];
char disk_io_message[] = "NetBSD/RVM: FFS create/write/fsync/seek/read/unlink verified\n";
char disk_fs_type[] = "ffs";
char disk_root_path[] = "/";
char *disk_fspec;
char disk_bss_path[] = "/sbin/bss-check";
char *disk_bss_argv[] = { disk_bss_path, 0 };
int disk_bss_status;
char disk_bss_message[] = "NetBSD/RVM: FHR BSS demand-zero and writable pages verified\n";
char disk_persist_path[] = "/etc/persist";
char disk_created_message[] = "NetBSD/RVM: FFS durable file created\n";
char disk_persist_message[] = "NetBSD/RVM: FFS persisted file verified after reboot\n";
char disk_script_message[] = "NetBSD/RVM: FHR shebang interpreter and argument forwarding verified\n";

int disk_main(long, char **, char **);
int
disk_main(long argc, char **argv, char **envp)
{
	if ((argc != 2 && argc != 3) || argv[0][0] != '/' || argv[argc] != 0 || envp[0] != 0)
		return 89;
	char *argument = argv[argc - 1];
	for (long i = 0; i < 70000; i++)
		if (argument[i] != 'A')
			return 90;
	if (argument[70000] != 0)
		return 90;
	if (argc == 3) {
		if (argv[1][0] != '/' || rvm_call(SYS_write, 1, (long)disk_script_message,
		    sizeof(disk_script_message) - 1, 0) < 0)
			return 90;
	}
	if (disk_pointer != disk_message || disk_zero[0] || disk_zero[65536] ||
	    disk_zero[sizeof(disk_zero) - 1])
		return 91;
	disk_zero[0] = 0x12;
	disk_zero[65536] = 0x34;
	disk_zero[sizeof(disk_zero) - 1] = 0x56;
	if (rvm_call(SYS_getpid, 0, 0, 0, 0) <= 1)
		return 92;
	long size = sizeof(disk_message) - 1;
	if (rvm_call(SYS_write, 1, (long)disk_pointer, size, 0) != size)
		return 93;
	long pid = rvm_call(SYS_fork, 0, 0, 0, 0);
	if (pid == 0) {
		rvm_call(SYS_execve, (long)disk_bss_path, (long)disk_bss_argv, 0, 0);
		return 99;
	}
	if (pid < 0 || rvm_call(SYS___wait450, pid, (long)&disk_bss_status, 0, 0) != pid ||
	    disk_bss_status != (41 << 8))
		return 99;
	size = sizeof(disk_bss_message) - 1;
	if (rvm_call(SYS_write, 1, (long)disk_bss_message, size, 0) != size)
		return 99;
	/* NetBSD mounts root read-only. Perform the same explicit remount
	 * that an ordinary userland startup would do, using native mount(2). */
	if (rvm_mount(SYS___mount50, (long)disk_fs_type, (long)disk_root_path,
	    0x10000, (long)&disk_fspec, sizeof(disk_fspec)) != 0)
		return 94;
	long fd = rvm_call(SYS_open, (long)disk_path, 0x602, 0600, 0);
	if (fd < 0)
		return 94;
	size = sizeof(disk_payload);
	if (rvm_call(SYS_write, fd, (long)disk_payload, size, 0) != size ||
	    rvm_call(SYS_fsync, fd, 0, 0, 0) != 0 ||
	    rvm_call(SYS_lseek, fd, 0, 0, 0) != 0 ||
	    rvm_call(SYS_read, fd, (long)disk_readback, size, 0) != size)
		return 95;
	for (long i = 0; i < size; i++)
		if (disk_readback[i] != disk_payload[i])
			return 96;
	if (rvm_call(SYS_close, fd, 0, 0, 0) != 0 ||
	    rvm_call(SYS_unlink, (long)disk_path, 0, 0, 0) != 0)
		return 97;
	fd = rvm_call(SYS_open, (long)disk_persist_path, 0, 0, 0);
	if (fd == -2) { /* ENOENT: this is the first boot of the fixture. */
		fd = rvm_call(SYS_open, (long)disk_persist_path, 0xa02, 0600, 0);
		if (fd < 0 || rvm_call(SYS_write, fd, (long)disk_payload, size, 0) != size ||
		    rvm_call(SYS_fsync, fd, 0, 0, 0) != 0 ||
		    rvm_call(SYS_close, fd, 0, 0, 0) != 0)
			return 100;
		if (rvm_call(SYS_write, 1, (long)disk_created_message,
		    sizeof(disk_created_message) - 1, 0) < 0)
			return 100;
	} else {
		if (fd < 0 || rvm_call(SYS_read, fd, (long)disk_readback, size, 0) != size ||
		    rvm_call(SYS_close, fd, 0, 0, 0) != 0)
			return 101;
		for (long i = 0; i < size; i++)
			if (disk_readback[i] != disk_payload[i])
				return 102;
		if (rvm_call(SYS_write, 1, (long)disk_persist_message,
		    sizeof(disk_persist_message) - 1, 0) < 0)
			return 102;
	}
	/* Flush metadata and return the fixture to a clean read-only root
	 * before its parent sleeps, so a normal emulator stop is repeatable. */
	if (rvm_mount(SYS___mount50, (long)disk_fs_type, (long)disk_root_path,
	    0x10001, (long)&disk_fspec, sizeof(disk_fspec)) != 0)
		return 97;
	size = sizeof(disk_io_message) - 1;
	if (rvm_call(SYS_write, 1, (long)disk_io_message, size, 0) != size)
		return 98;
	return 37;
}
