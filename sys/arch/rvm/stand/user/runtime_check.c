/* Ordinary C main linked against the bootstrap static runtime archive. */
#include "rvm_user.h"
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/signal.h>

int runtime_fds[2], runtime_status;
char runtime_buffer[32];
char runtime_file[] = "/etc/runtime-mode";
char runtime_payload[] = "native runtime file\n";
struct stat runtime_stat;
extern int heap_check(void);
extern int environment_check(void);
char *yes_argv[] = { "/usr/bin/yes", "netbsd-native-yes", 0 };

int main(int, char **, char **);
int
main(int argc, char **argv, char **envp)
{
	if (argc != 2 || strcmp(argv[1], "runtime-argument") ||
	    strcmp(__progname, "runtime-check") || environ != envp ||
	    !envp[0] || strcmp(envp[0], "RVM_RUNTIME=1") || envp[1] || getpid() <= 1)
		return 130;
	if (close(-1) != -1 || errno != EBADF ||
	    ioctl(-1, 0UL) != -1 || errno != EBADF ||
	    open("/etc/this-file-does-not-exist", O_RDONLY) != -1 || errno != ENOENT)
		return 131;
	(void)getpid();
	if (errno != ENOENT)
		return 131;
	memset(runtime_buffer, 0, sizeof(runtime_buffer));
	memcpy(runtime_buffer, "abcde", 6);
	memmove(runtime_buffer + 1, runtime_buffer, 6);
	if (strcmp(runtime_buffer, "aabcde") || strlen(runtime_buffer) != 6)
		return 135;
	int special = open("/dev/null", O_RDWR);
	if (special < 0 || read(special, runtime_buffer, sizeof(runtime_buffer)) != 0 ||
	    write(special, runtime_payload, sizeof(runtime_payload)) != sizeof(runtime_payload) || close(special))
		return 136;
	special = open("/dev/zero", O_RDONLY);
	memset(runtime_buffer, 0x5a, sizeof(runtime_buffer));
	if (special < 0 || read(special, runtime_buffer, sizeof(runtime_buffer)) != sizeof(runtime_buffer) || close(special))
		return 136;
	for (size_t i = 0; i < sizeof(runtime_buffer); i++)
		if (runtime_buffer[i] != 0) return 136;
	int fd = open(runtime_file, O_CREAT | O_TRUNC | O_RDWR, 0600U);
	if (fd < 0 || fstat(fd, &runtime_stat) != 0 ||
	    (runtime_stat.st_mode & 0777) != 0600 ||
	    write(fd, runtime_payload, sizeof(runtime_payload)) != sizeof(runtime_payload) ||
	    lseek(fd, 0, 0) != 0 ||
	    read(fd, runtime_buffer, sizeof(runtime_buffer)) != sizeof(runtime_payload) ||
	    strcmp(runtime_buffer, runtime_payload) || close(fd) || unlink(runtime_file))
		return 132;
	unsigned char *memory = mmap(0, 131072, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	if (memory == MAP_FAILED || memory[0] || memory[65536] || memory[131071])
		return 133;
	memory[0] = 0x12; memory[65536] = 0x34; memory[131071] = 0x56;
	if (memory[0] != 0x12 || memory[65536] != 0x34 || memory[131071] != 0x56 ||
	    munmap(memory, 131072) != 0)
		return 133;
	if (pipe(runtime_fds) != 0)
		return 134;
	pid_t pid = fork();
	if (pid == 0) {
		close(runtime_fds[0]);
		_exit(write(runtime_fds[1], "pipe", 4) == 4 ? 0 : 1);
	}
	close(runtime_fds[1]);
	if (pid < 0 || read(runtime_fds[0], runtime_buffer, sizeof(runtime_buffer)) != 4 ||
	    runtime_buffer[0] != 'p' || runtime_buffer[3] != 'e' ||
	    waitpid(pid, &runtime_status, 0) != pid || runtime_status != 0 || close(runtime_fds[0]))
		return 134;
	int heap_error = heap_check();
	if (heap_error) return heap_error;
	int environment_error = environment_check();
	if (environment_error) return environment_error;
	/* Run the tree's unmodified yes(1), then close its pipe and verify
	 * native SIGPIPE containment rather than leaving an infinite writer. */
	if (pipe(runtime_fds) != 0) return 149;
	pid = fork();
	if (pid == 0) {
		close(runtime_fds[0]);
		if (dup2(runtime_fds[1], 1) < 0) _exit(1);
		close(runtime_fds[1]);
		execve(yes_argv[0], yes_argv, environ);
		_exit(127);
	}
	close(runtime_fds[1]);
	size_t received = 0;
	while (received < 18) {
		ssize_t n = read(runtime_fds[0], runtime_buffer + received, 18 - received);
		if (n <= 0) return 149;
		received += n;
	}
	if (memcmp(runtime_buffer, "netbsd-native-yes\n", 18) != 0 || close(runtime_fds[0]) ||
	    waitpid(pid, &runtime_status, 0) != pid || (runtime_status & 127) != SIGPIPE)
		return 149;
	puts("NetBSD/RVM: unchanged NetBSD yes(1), pipe output and SIGPIPE containment verified");
	puts("NetBSD/RVM: native static crt0, errno, variadic open, mmap, pipe and C exit verified");
	return 43;
}
