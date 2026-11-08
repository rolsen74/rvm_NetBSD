/* Native syscall veneers needed by the static NetBSD utility/shell build. */
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <lwp.h>
#include <time.h>
#include <unistd.h>
extern long __rvm_rawcall(long, long, long, long, long, long, long, long, long);
#define CALL(n,a,b,c,d,e,f,g,h) __rvm_rawcall(SYS_##n,(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),(long)(f),(long)(g),(long)(h))
static long result(long r) { if (r < 0) { errno = -r; return -1; } return r; }
int dup(int fd) { return result(CALL(dup,fd,0,0,0,0,0,0,0)); }
int chdir(const char *p) { return result(CALL(chdir,p,0,0,0,0,0,0,0)); }
int fchdir(int fd) { return result(CALL(fchdir,fd,0,0,0,0,0,0,0)); }
int access(const char *p, int mode) { return result(CALL(access,p,mode,0,0,0,0,0,0)); }
int chmod(const char *p, mode_t mode) { return result(CALL(chmod,p,mode,0,0,0,0,0,0)); }
int mkdir(const char *p, mode_t mode) { return result(CALL(mkdir,p,mode,0,0,0,0,0,0)); }
int rmdir(const char *p) { return result(CALL(rmdir,p,0,0,0,0,0,0,0)); }
int fsync(int fd) { return result(CALL(fsync,fd,0,0,0,0,0,0,0)); }
int undelete(const char *p) { return result(CALL(undelete,p,0,0,0,0,0,0,0)); }
mode_t umask(mode_t mode) { return CALL(umask,mode,0,0,0,0,0,0,0); }
uid_t geteuid(void) { return CALL(geteuid,0,0,0,0,0,0,0,0); }
gid_t getegid(void) { return CALL(getegid,0,0,0,0,0,0,0,0); }
pid_t getppid(void) { return CALL(getppid,0,0,0,0,0,0,0,0); }
pid_t getpgrp(void) { return CALL(getpgrp,0,0,0,0,0,0,0,0); }
int setpgid(pid_t pid, pid_t pgid) { return result(CALL(setpgid,pid,pgid,0,0,0,0,0,0)); }
int kill(pid_t pid, int sig) { return result(CALL(kill,pid,sig,0,0,0,0,0,0)); }
int setrlimit(int which, const struct rlimit *p) { return result(CALL(setrlimit,which,p,0,0,0,0,0,0)); }
int getrusage(int who, struct rusage *p) { return result(CALL(__getrusage50,who,p,0,0,0,0,0,0)); }
int getgroups(int n, gid_t *p) { return result(CALL(getgroups,n,p,0,0,0,0,0,0)); }
int setuid(uid_t uid) { return result(CALL(setuid,uid,0,0,0,0,0,0,0)); }
int setgid(gid_t gid) { return result(CALL(setgid,gid,0,0,0,0,0,0,0)); }
int dup3(int from, int to, int flags) { return result(CALL(__dup3100,from,to,flags,0,0,0,0,0)); }
int getdents(int fd, char *p, size_t n) { return result(CALL(__getdents30,fd,p,n,0,0,0,0,0)); }
int fstatvfs1(int fd, struct statvfs *p, int flags) { return result(CALL(__fstatvfs190,fd,p,flags,0,0,0,0,0)); }
int mount(const char *type, const char *path, int flags, void *data, size_t size) {
    return result(CALL(__mount50,type,path,flags,data,size,0,0,0));
}
lwpid_t _lwp_self(void) { return CALL(_lwp_self,0,0,0,0,0,0,0,0); }
int _lwp_kill(lwpid_t lid, int sig) { return result(CALL(_lwp_kill,lid,sig,0,0,0,0,0,0)); }
int sigprocmask(int how, const sigset_t *s, sigset_t *old) { return result(CALL(__sigprocmask14,how,s,old,0,0,0,0,0)); }
int sigpending(sigset_t *s) { return result(CALL(__sigpending14,s,0,0,0,0,0,0,0)); }
int sigsuspend(const sigset_t *s) { return result(CALL(__sigsuspend14,s,0,0,0,0,0,0,0)); }
extern void __rvm_sigtramp(void);
int sigaction(int sig, const struct sigaction *s, struct sigaction *old) {
    return result(CALL(__sigaction_sigtramp,sig,s,old,__rvm_sigtramp,2,0,0,0));
}
int gettimeofday(struct timeval *p, void *zone) { return result(CALL(__gettimeofday50,p,zone,0,0,0,0,0,0)); }
int clock_gettime(clockid_t which, struct timespec *p) { return result(CALL(__clock_gettime50,which,p,0,0,0,0,0,0)); }
int clock_nanosleep(clockid_t which, int flags, const struct timespec *req, struct timespec *rem) {
    long r = CALL(clock_nanosleep,which,flags,req,rem,0,0,0,0);
    return r < 0 ? -r : r;
}
time_t time(time_t *p) { struct timeval tv; if (gettimeofday(&tv, 0)) return -1; if(p) *p=tv.tv_sec; return tv.tv_sec; }
int _kill(pid_t, int);
int _kill(pid_t pid, int sig) { return kill(pid, sig); }
