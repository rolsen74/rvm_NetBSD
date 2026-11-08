/* Static-runtime random API: obtain every byte from the native kernel CSPRNG.
 * This avoids a userspace PRNG/TLS state until full libc thread support lands.
 */
#include <sys/types.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
extern long __rvm_rawcall(long, long, long, long, long, long, long, long, long);
ssize_t getrandom(void *p, size_t n, unsigned int flags) {
    long r = __rvm_rawcall(SYS_getrandom,(long)p,n,flags,0,0,0,0,0);
    if (r < 0) { errno = -r; return -1; } return r;
}
void arc4random_buf(void *p, size_t n) {
    unsigned char *out = p;
    while (n) {
        ssize_t r = getrandom(out, n, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) abort();
        out += r; n -= r;
    }
}
uint32_t arc4random(void) { uint32_t r; arc4random_buf(&r, sizeof(r)); return r; }
uint32_t arc4random_uniform(uint32_t bound) {
    if (bound < 2) return 0;
    uint32_t minimum = -bound % bound, r;
    do { r = arc4random(); } while (r < minimum);
    return r % bound;
}
uint32_t _arc4random(void);
void _arc4random_buf(void *, size_t);
uint32_t _arc4random_uniform(uint32_t);
uint32_t _arc4random(void) { return arc4random(); }
void _arc4random_buf(void *p, size_t n) { arc4random_buf(p, n); }
uint32_t _arc4random_uniform(uint32_t n) { return arc4random_uniform(n); }
