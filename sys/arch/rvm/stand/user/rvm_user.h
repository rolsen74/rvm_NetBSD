/* Bootstrap static user runtime; not the full NetBSD libc. */
#ifndef _RVM_BOOTSTRAP_USER_H_
#define _RVM_BOOTSTRAP_USER_H_
#include <sys/types.h>
#include <sys/timespec.h>
extern char **environ;
extern char *__progname;
int *__errno(void);
#define errno (*__errno())
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
int open(const char *, int, ...);
int close(int);
int unlink(const char *);
struct stat;
int fstat(int, struct stat *) __RENAME(__fstat50);
int dup2(int, int);
int pipe(int [2]);
pid_t fork(void);
pid_t getpid(void);
pid_t setsid(void);
pid_t waitpid(pid_t, int *, int);
int execve(const char *, char *const [], char *const []);
int execvp(const char *, char *const []);
int ioctl(int, unsigned long, ...);
int nanosleep(const struct timespec *, struct timespec *) __RENAME(__nanosleep50);
off_t lseek(int, off_t, int);
void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);
int brk(void *);
void *sbrk(intptr_t);
int getpagesize(void);
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
int posix_memalign(void **, size_t, size_t);
char *getenv(const char *);
int setenv(const char *, const char *, int);
int unsetenv(const char *) __RENAME(__unsetenv13);
int putenv(char *) __RENAME(__putenv50);
int atexit(void (*)(void));
size_t strlcpy(char *, const char *, size_t);
void _exit(int) __attribute__((noreturn));
void exit(int) __attribute__((noreturn));
size_t strlen(const char *);
int strcmp(const char *, const char *);
int memcmp(const void *, const void *, size_t);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int puts(const char *);
long __rvm_rawcall(long, long, long, long, long, long, long, long, long);
#endif
