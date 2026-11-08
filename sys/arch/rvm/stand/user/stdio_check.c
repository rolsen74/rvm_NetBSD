/* Exercise the actual NetBSD FILE implementation and formatter in the guest. */
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

int stdio_check(void);
static unsigned char block[4096];
static char small_buffer[17];
static FILE *exit_stream;
static void exit_message(void) { fputs("callback", exit_stream); }
struct writer { char bytes[32]; size_t count; int fail; };
static ssize_t short_write(void *cookie, const void *data, size_t size) {
	struct writer *w = cookie;
	if (w->fail) { errno = EIO; return -1; }
	if (size > 3) size = 3;
	if (size > sizeof(w->bytes) - w->count) { errno = ENOSPC; return -1; }
	memcpy(w->bytes + w->count, data, size); w->count += size;
	return size;
}
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "stdio failure at line %d errno=%d\n", __LINE__, errno); return 159; } } while (0)

int
stdio_check(void)
{
	char text[160];
	CHECK(snprintf(text, sizeof(text), "%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",
	    1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 20 && !strcmp(text, "1:2:3:4:5:6:7:8:9:10"));
	CHECK(snprintf(text, sizeof(text), "%lld/%#018llx/%zu", (long long)INT64_MIN,
	    0x123456789abcdef0ULL, (size_t)65536) == 45 &&
	    !strcmp(text, "-9223372036854775808/0x123456789abcdef0/65536"));
	CHECK(snprintf(text, sizeof(text), "%3$*2$.*1$s", 3, 7, "abcdef") == 7 && !strcmp(text, "    abc"));
	CHECK(snprintf(text, 4, "abcdef") == 6 && !strcmp(text, "abc"));
	CHECK(snprintf(NULL, 0, "%s", "count-only") == 10);
	int count = -1;
	CHECK(snprintf(text, sizeof(text), "abc%nDEF", &count) == 6 && count == 3);
	volatile double x = 1.25, y = 2.5;
	volatile float sx = 1.25f, sy = 2.5f;
	CHECK(x + y == 3.75 && x * y == 3.125 && y / x == 2.0);
	CHECK(snprintf(text, sizeof(text), "%.2f/%.3g/%.1e/%.2a", x + y, x * y, y / x, x) > 0 &&
	    !strcmp(text, "3.75/3.12/2.0e+00/0x1.40p+0"));
	CHECK(snprintf(text, sizeof(text), "%.2f/%.1Lf", (double)(sx + sy), (long double)1.5L) > 0 &&
	    !strcmp(text, "3.75/1.5"));
	CHECK(snprintf(text, sizeof(text), "%.0f/%.0f/%+.1f", 2.5, 3.5, -0.0) > 0 && !strcmp(text, "2/4/-0.0"));
	char *end;
	CHECK(strtod("1.25tail", &end) == 1.25 && !strcmp(end, "tail"));
	CHECK(strtof("3.75tail", &end) == 3.75f && !strcmp(end, "tail"));
	double special = strtod("Inf", &end);
	CHECK(special > 1e300 && *end == '\0');
	special = strtod("NaN", &end);
	CHECK(special != special && *end == '\0');
	CHECK(strtod(".inf", &end) == 0 && *end == '.');
	union { double d; uint64_t u; } tiny = { strtod("4.9406564584124654e-324", &end) };
	CHECK(tiny.u == 1 && *end == '\0');
	int signed_value = 0, read_count = 0; unsigned hex = 0;
	float scanned_float = 0; double scanned_double = 0;
	long double scanned_long_double = 0;
	char scanned_word[16], scanned_set[8];
	CHECK(sscanf("-42 ff word 1.25 2.5 3.75 abc!", "%d %x %15s %f %lf %Lf %7[a-z]%n",
	    &signed_value, &hex, scanned_word, &scanned_float, &scanned_double,
	    &scanned_long_double, scanned_set, &read_count) == 7);
	CHECK(signed_value == -42 && hex == 255 && !strcmp(scanned_word, "word") &&
	    scanned_float == 1.25f && scanned_double == 2.5 && scanned_long_double == 3.75L &&
	    !strcmp(scanned_set, "abc") && read_count == 29);
	wchar_t wide[40], wide_word[8];
	CHECK(swprintf(wide, 40, L"%ls:%d/%.2f", L"wide", 42, 1.25) == 12 &&
	    !wcscmp(wide, L"wide:42/1.25"));
	CHECK(swscanf(L"wide 42", L"%7ls %d", wide_word, &signed_value) == 2 &&
	    !wcscmp(wide_word, L"wide") && signed_value == 42);
	CHECK(sscanf("", "%d", &signed_value) == EOF && sscanf("x", "%d", &signed_value) == 0);
	FILE *temporary = tmpfile();
	struct stat tempstat;
	CHECK(temporary && fstat(fileno(temporary), &tempstat) == 0 && tempstat.st_nlink == 0 &&
	    (tempstat.st_mode & 0777) == 0600);
	CHECK(fprintf(temporary, "temporary:%d", 42) == 12 && fseek(temporary, 0, SEEK_SET) == 0 &&
	    fscanf(temporary, "temporary:%d", &signed_value) == 1 && signed_value == 42 && fclose(temporary) == 0);
	char tempname[] = "/tmp/rvm-XXXXXX.tail";
	int tempfd = mkstemps(tempname, 5);
	CHECK(tempfd >= 0 && close(tempfd) == 0 && unlink(tempname) == 0);
	char dirname[] = "/tmp/rvm-dir-XXXXXX";
	CHECK(mkdtemp(dirname) == dirname && stat(dirname, &tempstat) == 0 &&
	    (tempstat.st_mode & 0777) == 0700 && rmdir(dirname) == 0);
	wchar_t *wide_allocated = NULL; size_t wide_length = 0;
	FILE *wide_file = open_wmemstream(&wide_allocated, &wide_length);
	CHECK(wide_file && fwprintf(wide_file, L"%ls %d", L"stream", 42) == 9 && fflush(wide_file) == 0 &&
	    wide_length == 9 && !wcscmp(wide_allocated, L"stream 42") && fclose(wide_file) == 0);
	free(wide_allocated);
	wide_file = tmpfile();
	CHECK(wide_file && fputwc(L'A', wide_file) == L'A' && fputws(L"BC", wide_file) >= 0 && fwide(wide_file, 0) > 0);
	CHECK(fseek(wide_file, 0, SEEK_SET) == 0 && fgetwc(wide_file) == L'A' &&
	    ungetwc(L'Q', wide_file) == L'Q' && fgetwc(wide_file) == L'Q' &&
	    fgetws(wide, 40, wide_file) && !wcscmp(wide, L"BC") && fclose(wide_file) == 0);
	FILE *file = fopen("/etc/stdio-check", "w+");
	CHECK(file != NULL && setvbuf(file, small_buffer, _IOFBF, sizeof(small_buffer)) == 0);
	for (unsigned i = 0; i < 33; i++) {
		for (size_t j = 0; j < sizeof(block); j++) block[j] = (unsigned char)(i + j);
		CHECK(fwrite(block, 1, sizeof(block), file) == sizeof(block));
	}
	CHECK(ftello(file) == 33 * 4096 && fflush(file) == 0);
	off_t big = ((off_t)1 << 33) + 7;
	CHECK(fseeko(file, big, SEEK_SET) == 0 && ftello(file) == big);
	CHECK(fseeko(file, 0, SEEK_SET) == 0);
	for (unsigned i = 0; i < 33; i++) {
		CHECK(fread(block, 1, sizeof(block), file) == sizeof(block));
		for (size_t j = 0; j < sizeof(block); j++) CHECK(block[j] == (unsigned char)(i + j));
	}
	CHECK(fgetc(file) == EOF && feof(file) && !ferror(file));
	CHECK(ungetc('Q', file) == 'Q' && !feof(file) && getc(file) == 'Q' && getc(file) == EOF);
	clearerr(file); CHECK(!feof(file) && !ferror(file));
	rewind(file); CHECK(fgetc(file) == 0);
	fpos_t position;
	CHECK(fgetpos(file, &position) == 0 && fgetc(file) == 1 && fsetpos(file, &position) == 0 && fgetc(file) == 1);
	CHECK(fclose(file) == 0 && unlink("/etc/stdio-check") == 0);
	file = fopen("/etc/motd", "r");
	CHECK(file && fputc('!', file) == EOF && errno == EBADF);
	clearerr(file); CHECK(!ferror(file) && fgetc(file) == 'W' && fclose(file) == 0);
	char memory[32] = {0};
	file = fmemopen(memory, sizeof(memory), "w+");
	CHECK(file && fputs("memory stream", file) >= 0 && fflush(file) == 0 && !strcmp(memory, "memory stream"));
	CHECK(fseek(file, 0, SEEK_SET) == 0 && fgets(text, sizeof(text), file) && !strcmp(text, "memory stream"));
	CHECK(fclose(file) == 0);
	char *allocated = NULL; size_t length = 0;
	file = open_memstream(&allocated, &length);
	CHECK(file && fprintf(file, "%s:%d", "stream", 42) == 9 && fflush(file) == 0);
	CHECK(length == 9 && !strcmp(allocated, "stream:42") && fclose(file) == 0);
	free(allocated);
	FILE *streams[48];
	for (unsigned i = 0; i < 48; i++) CHECK((streams[i] = fmemopen(NULL, 16, "w+")) != NULL);
	for (unsigned i = 0; i < 48; i++) CHECK(fputs("slot", streams[i]) >= 0);
	CHECK(fflush(NULL) == 0);
	for (unsigned i = 0; i < 48; i++) CHECK(fclose(streams[i]) == 0);
	struct writer w = {0};
	file = funopen2(&w, NULL, short_write, NULL, NULL, NULL);
	CHECK(file && fwrite("partial writes", 1, 14, file) == 14 && fflush(file) == 0 && w.count == 14);
	CHECK(!memcmp(w.bytes, "partial writes", 14));
	w.fail = 1;
	CHECK(fputs("error", file) >= 0 && fflush(file) == EOF && ferror(file));
	clearerr(file); CHECK(!ferror(file));
	CHECK(fclose(file) == 0);
	CHECK(fflush(NULL) == 0);
	int fds[2], status; CHECK(pipe(fds) == 0);
	pid_t pid = fork();
	if (pid == 0) {
		close(fds[0]); exit_stream = fdopen(fds[1], "w");
		if (!exit_stream || setvbuf(exit_stream, NULL, _IOFBF, 4096) ||
		    fputs("buffered-", exit_stream) < 0 || atexit(exit_message)) _exit(1);
		exit(23);
	}
	CHECK(pid > 0); close(fds[1]);
	size_t done = 0;
	while (done < 17) { ssize_t n = read(fds[0], text + done, 17 - done); CHECK(n > 0); done += n; }
	CHECK(close(fds[0]) == 0 && waitpid(pid, &status, 0) == pid && status == (23 << 8));
	CHECK(!memcmp(text, "buffered-callback", 17));
	puts("NetBSD/RVM: stdio buffering, seek/EOF, memory streams, formatting, short I/O and exit flushing verified");
	return 0;
}
