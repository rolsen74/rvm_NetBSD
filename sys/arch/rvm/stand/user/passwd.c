/* Bootstrap files-only account lookup. NetBSD's pw_scan parses /etc/passwd;
 * the NSS dispatch/database backends are not part of this static runtime yet.
 */
#include <sys/types.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
extern int __pw_scan(char *, struct passwd *, int *);
static struct passwd entry;
static char line[1024];
static struct passwd *
lookup(const char *name, uid_t uid)
{
    int saved = errno;
    FILE *fp = fopen("/etc/passwd", "r");
    if (!fp) return NULL;
    while (fgets(line, sizeof(line), fp)) {
        char *end = strchr(line, '\n');
        if (!end) {
            int c;
            while ((c = fgetc(fp)) != '\n' && c != EOF) { }
            continue;
        }
        *end = '\0';
        int flags = _PASSWORD_OLDFMT | _PASSWORD_NOWARN;
        if (line[0] == '#' || !__pw_scan(line, &entry, &flags)) continue;
        if (name ? !strcmp(name, entry.pw_name) : uid == entry.pw_uid) {
            fclose(fp); errno = saved; return &entry;
        }
    }
    int error = ferror(fp) ? errno : saved;
    fclose(fp); errno = error; return NULL;
}
struct passwd *getpwnam(const char *name) { return lookup(name, 0); }
struct passwd *getpwuid(uid_t uid) { return lookup(NULL, uid); }

/* Files-only getusershell(3), with the traditional fallback when absent. */
static FILE *shells;
static unsigned fallback;
static char shell_line[1024];
void endusershell(void) { if (shells) fclose(shells); shells = NULL; fallback = 0; }
void setusershell(void) { endusershell(); }
char *getusershell(void) {
    static char *const defaults[] = { "/bin/sh", "/bin/csh" };
    if (!shells && fallback == 0) {
        shells = fopen("/etc/shells", "r");
        if (!shells) fallback = 1;
    }
    if (fallback) return fallback <= 2 ? defaults[fallback++ - 1] : NULL;
    while (fgets(shell_line, sizeof(shell_line), shells)) {
        char *p = shell_line;
        p[strcspn(p, "#\r\n")] = '\0';
        while (*p == ' ' || *p == '\t') p++;
        char *end = p + strcspn(p, " \t"); *end = '\0';
        if (*p == '/') return p;
    }
    return NULL;
}
void _setusershell(void);
char *_getusershell(void);
void _setusershell(void) { setusershell(); }
char *_getusershell(void) { return getusershell(); }

/* These database stay-open hints need no retained descriptor for file lookups. */
int setpassent(int stayopen) { (void)stayopen; return 1; }
void endpwent(void) { }

#include <grp.h>
#include <stdlib.h>
#include <limits.h>
static struct group group;
static char group_line[1024];
static char *group_members[64];
static struct group *
group_lookup(const char *name, gid_t gid)
{
    int saved = errno;
    FILE *fp = fopen("/etc/group", "r");
    if (!fp) return NULL;
    while (fgets(group_line, sizeof(group_line), fp)) {
        char *end = strchr(group_line, '\n');
        if (!end) {
            int c; while ((c = fgetc(fp)) != '\n' && c != EOF) { }
            continue;
        }
        *end = '\0';
        char *p = group_line;
        group.gr_name = strsep(&p, ":");
        group.gr_passwd = strsep(&p, ":");
        char *number = strsep(&p, ":"), *tail;
        if (!number || !*number || !p || *group.gr_name == '#') continue;
        errno = 0;
        unsigned long id = strtoul(number, &tail, 10);
        if (*tail || errno || id > GID_MAX) continue;
        group.gr_gid = id;
        unsigned count = 0;
        while (p && *p && count < 63) group_members[count++] = strsep(&p, ",");
        if (p && *p) continue;
        group_members[count] = NULL;
        group.gr_mem = group_members;
        if (name ? !strcmp(name, group.gr_name) : gid == group.gr_gid) {
            fclose(fp); errno = saved; return &group;
        }
    }
    int error = ferror(fp) ? errno : saved;
    fclose(fp); errno = error; return NULL;
}
struct group *getgrnam(const char *name) { return group_lookup(name, 0); }
struct group *getgrgid(gid_t gid) { return group_lookup(NULL, gid); }
int setgroupent(int stayopen) { (void)stayopen; return 1; }
void endgrent(void) { }
int _setpassent(int);
void _endpwent(void);
int _setgroupent(int);
void _endgrent(void);
struct group *_getgrnam(const char *);
struct group *_getgrgid(gid_t);
int _setpassent(int stay) { return setpassent(stay); }
void _endpwent(void) { endpwent(); }
int _setgroupent(int stay) { return setgroupent(stay); }
void _endgrent(void) { endgrent(); }
struct group *_getgrnam(const char *name) { return getgrnam(name); }
struct group *_getgrgid(gid_t gid) { return getgrgid(gid); }
