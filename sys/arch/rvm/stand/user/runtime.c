/* Small single-threaded static runtime for port bring-up. Uses native NetBSD
 * syscall numbers, structures and errno values. Emulated TLS is single-LWP;
 * a dynamic linker and thread support remain unported. Stdio, allocator,
 * environment and atexit routines come from NetBSD libc.
 */
#include "rvm_user.h"
#include <sys/syscall.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <unistd.h>

char **environ;
char *__progname;
struct ps_strings;
struct ps_strings *__ps_strings;
static int runtime_errno;
int *__errno(void) { return &runtime_errno; }
long __rvm_error(long);
long __rvm_error(long error) { errno = error; return -1; }

static long
result(long value)
{
	if (value < 0) { errno = -value; return -1; }
	return value;
}
#define CALL(n,a,b,c,d,e,f,g,h) __rvm_rawcall(SYS_##n, (long)(a), (long)(b), \
    (long)(c), (long)(d), (long)(e), (long)(f), (long)(g), (long)(h))

ssize_t read(int fd, void *p, size_t n) { return result(CALL(read,fd,p,n,0,0,0,0,0)); }
ssize_t write(int fd, const void *p, size_t n) { return result(CALL(write,fd,p,n,0,0,0,0,0)); }
int close(int fd) { return result(CALL(close,fd,0,0,0,0,0,0,0)); }
int unlink(const char *p) { return result(CALL(unlink,p,0,0,0,0,0,0,0)); }
int fstat(int fd, struct stat *s) { return result(CALL(__fstat50,fd,s,0,0,0,0,0,0)); }
int stat(const char *p, struct stat *s) { return result(CALL(__stat50,p,s,0,0,0,0,0,0)); }
int lstat(const char *p, struct stat *s) { return result(CALL(__lstat50,p,s,0,0,0,0,0,0)); }
int __getcwd(char *, size_t);
int __getcwd(char *p, size_t n) { return result(CALL(__getcwd,p,n,0,0,0,0,0,0)); }
int dup2(int fd, int to) { return result(CALL(dup2,fd,to,0,0,0,0,0,0)); }
int pipe(int fd[2]) { return result(CALL(pipe2,fd,0,0,0,0,0,0,0)); }
pid_t fork(void) { return result(CALL(fork,0,0,0,0,0,0,0,0)); }
pid_t getpid(void) { return result(CALL(getpid,0,0,0,0,0,0,0,0)); }
pid_t setsid(void) { return result(CALL(setsid,0,0,0,0,0,0,0,0)); }
pid_t waitpid(pid_t pid, int *status, int options) {
	return result(CALL(__wait450,pid,status,options,0,0,0,0,0));
}
int execve(const char *path, char *const argv[], char *const envp[]) {
	return result(CALL(execve,path,argv,envp,0,0,0,0,0));
}
int nanosleep(const struct timespec *req, struct timespec *rem) {
	return result(CALL(__nanosleep50,req,rem,0,0,0,0,0,0));
}
unsigned int sleep(unsigned int seconds) {
	struct timespec req = { seconds, 0 }, rem;
	if (nanosleep(&req, &rem) == -1 && errno == EINTR)
		return rem.tv_sec + (rem.tv_nsec != 0);
	return 0;
}
unsigned int _sleep(unsigned int);
unsigned int _sleep(unsigned int n) { return sleep(n); }
size_t _strlcpy(char *, const char *, size_t);
size_t _strlcpy(char *d, const char *s, size_t n) { return strlcpy(d, s, n); }
off_t lseek(int fd, off_t off, int whence) { return result(CALL(lseek,fd,0,off,whence,0,0,0,0)); }
void *mmap(void *addr, size_t n, int prot, int flags, int fd, off_t off) {
	return (void *)result(CALL(mmap,addr,n,prot,flags,fd,0,off,0));
}
void *_mmap(void *, size_t, int, int, int, off_t);
void *_mmap(void *addr, size_t n, int prot, int flags, int fd, off_t off) {
	return mmap(addr, n, prot, flags, fd, off);
}
int munmap(void *addr, size_t n) { return result(CALL(munmap,addr,n,0,0,0,0,0,0)); }
void sync(void) { CALL(sync,0,0,0,0,0,0,0,0); }
uid_t getuid(void) { return CALL(getuid,0,0,0,0,0,0,0,0); }
gid_t getgid(void) { return CALL(getgid,0,0,0,0,0,0,0,0); }
int issetugid(void) { return CALL(issetugid,0,0,0,0,0,0,0,0); }
int madvise(void *p, size_t n, int advice) { return result(CALL(madvise,p,n,advice,0,0,0,0,0)); }
ssize_t readlink(const char *p, char *buf, size_t n) { return result(CALL(readlink,p,buf,n,0,0,0,0,0)); }
ssize_t _readlink(const char *, char *, size_t);
ssize_t _readlink(const char *p, char *buf, size_t n) { return readlink(p,buf,n); }
int __sysctl(const int *, unsigned, void *, size_t *, const void *, size_t);
int __sysctl(const int *mib, unsigned n, void *old, size_t *len, const void *newp, size_t newlen) {
	return result(CALL(__sysctl,mib,n,old,len,newp,newlen,0,0));
}
void abort(void) __attribute__((noreturn));
void abort(void) { CALL(kill,getpid(),SIGABRT,0,0,0,0,0,0); _exit(127); }

/* Early static runtime assertion reporting does not depend on syslogd. */
void __assert13(const char *, int, const char *, const char *) __attribute__((noreturn));
void __assert13(const char *file, int line, const char *function, const char *expression) {
	const char *parts[] = { "assertion failed: ", expression, " in ",
	    function ? function : "?", " (", file, ")\n" };
	(void)line;
	for (unsigned i = 0; i < sizeof(parts)/sizeof(parts[0]); i++)
		write(2, parts[i], strlen(parts[i]));
	abort();
}

int
open(const char *path, int flags, ...)
{
	unsigned mode = 0;
	if (flags & O_CREAT) {
		__builtin_va_list ap;
		__builtin_va_start(ap, flags);
		mode = __builtin_va_arg(ap, unsigned);
		__builtin_va_end(ap);
	}
	return result(CALL(open,path,flags,mode,0,0,0,0,0));
}

void _exit(int status) { CALL(exit,status,0,0,0,0,0,0,0); for (;;) { } }
void (*__cleanup)(void);
extern void __cxa_finalize(void *);
extern void __rvm_run_init(void);
extern void __rvm_run_fini(void);
void exit(int status) {
	__cxa_finalize(0);
	__rvm_run_fini();
	/* Destructors may register additional exit handlers. */
	__cxa_finalize(0);
	if (__cleanup) (*__cleanup)();
	_exit(status);
}
void _Exit(int) __attribute__((noreturn));
void _Exit(int status) { _exit(status); }

/* Private libc syscall names retain the same native ABI. */
ssize_t _read(int, void *, size_t);
ssize_t _read(int fd, void *p, size_t n) { return read(fd, p, n); }
off_t _lseek(int, off_t, int);
off_t _lseek(int fd, off_t off, int whence) { return lseek(fd, off, whence); }
long pathconf(const char *, int);
long pathconf(const char *p, int name) { return result(CALL(pathconf,p,name,0,0,0,0,0,0)); }
struct rlimit;
int getrlimit(int, struct rlimit *);
int getrlimit(int which, struct rlimit *limit) { return result(CALL(getrlimit,which,limit,0,0,0,0,0,0)); }

extern int main(int, char **, char **);
void __rvm_start(long, char **, char **, struct ps_strings *) __attribute__((noreturn));
void
__rvm_start(long argc, char **argv, char **envp, struct ps_strings *ps)
{
	__ps_strings = ps;
	environ = envp;
	__progname = argc > 0 ? argv[0] : "";
	for (char *p = __progname; *p; p++)
		if (*p == '/') __progname = p + 1;
	__rvm_run_init();
	exit(main(argc, argv, envp));
}
