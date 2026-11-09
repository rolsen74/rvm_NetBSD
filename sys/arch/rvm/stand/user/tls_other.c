/* External and file-local TLS definitions must keep distinct descriptors. */
#include <stdint.h>
extern __thread int tls_number;
static __thread uint64_t private_word = UINT64_C(0x123456789abcdef0);
int *tls_other_number(void) { return &tls_number; }
uint64_t *tls_other_word(void) { return &private_word; }
