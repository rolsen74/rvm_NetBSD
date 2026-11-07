/* The backend uses bzero for block clearing even in freestanding code. */
#include <sys/types.h>
void bzero(void *, size_t);
#include "../../../../../common/lib/libc/string/bzero.c"
