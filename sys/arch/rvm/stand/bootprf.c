/* Console/panic wrappers around NetBSD's unchanged kprintf parser. */
extern const char rvm_uvm_panic[];
void vprintf(const char *fmt, va_list ap)
{
	unsigned int guard = UINT_MAX;
	if (curlwp != NULL) guard = rvm_intr_suspend();
	else rvm_intr_disable();
	kprintf(fmt, TOCONS, NULL, NULL, ap);
	if (guard != UINT_MAX) rvm_intr_resume(guard);
}
void printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
}
void panic(const char *fmt, ...)
{
	va_list ap;
	rvm_intr_disable();
	rvm_early_puts(rvm_uvm_panic);
	va_start(ap, fmt); kprintf(fmt, TOCONS, NULL, NULL, ap); va_end(ap);
	rvm_early_putchar('\n'); rvm_boot_stop();
}
const char rvm_prf_format[] = "%s %d %u %#lx %016llx %zu %td %.3s %c";
const char rvm_prf_string[] = "hello";
const char rvm_prf_expected[] = "hello -7 4000000000 0x1234 00000000000000ab 99 -13 hel !";
const char rvm_prf_empty[] = "";
int rvm_prf_verify(void)
{
	char buf[128], tiny[4]; int n;
	n = snprintf(buf, sizeof(buf), rvm_prf_format, rvm_prf_string, -7,
	    4000000000U, 0x1234UL, 0xabULL, (size_t)99, (ptrdiff_t)-13,
	    rvm_prf_string, '!');
	if (n != (int)strlen(rvm_prf_expected) || strcmp(buf, rvm_prf_expected)) return 0;
	memset(tiny, 0xa5, sizeof(tiny));
	if (snprintf(tiny, 3, "%s", rvm_prf_string) != 5 || tiny[0] != 'h' ||
	    tiny[1] != 'e' || tiny[2] != 0 || (unsigned char)tiny[3] != 0xa5) return 0;
	if (snprintf(NULL, 0, "%s %ld", rvm_prf_string, -123L) != 10) return 0;
	if (snprintf(buf, sizeof(buf), "%*.*s", 5, 3, rvm_prf_string) != 5 ||
	    buf[0] != ' ' || buf[1] != ' ' || buf[2] != 'h' || buf[4] != 'l' || buf[5] != 0) return 0;
	return snprintf(buf, sizeof(buf), "%s", rvm_prf_empty) == 0 && buf[0] == 0;
}
