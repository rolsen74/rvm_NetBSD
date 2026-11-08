/* Host-side SDL event injector for a headless keyboard integration test.
 * The guest still reads GeneralDriver and processes normal tty input. */
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>

int
SDL_PollEvent(SDL_Event *event)
{
	static int (*real_poll)(SDL_Event *);
	static int fd = -1, release;
	static unsigned char previous;
	static long long next_key;
	unsigned char ch;
	if (real_poll == NULL)
		real_poll = dlsym(RTLD_NEXT, "SDL_PollEvent");
	if (real_poll(event))
		return 1;
	if (fd < 0) {
		const char *path = getenv("RVM_TEST_KEYFILE");
		if (path == NULL) return 0;
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) return 0;
	}
	if (release) {
		ch = previous;
	} else {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		long long ns = (long long)now.tv_sec * 1000000000 + now.tv_nsec;
		if (ns < next_key || read(fd, &ch, 1) != 1)
			return 0;
		next_key = ns + 30000000;
	}
	memset(event, 0, sizeof(*event));
	event->type = release ? SDL_KEYUP : SDL_KEYDOWN;
	event->key.state = release ? SDL_RELEASED : SDL_PRESSED;
	event->key.keysym.sym = ch == '\n' ? SDLK_RETURN :
	    ch == '\b' ? SDLK_BACKSPACE : ch;
	/* Real SDL letter keycodes remain lowercase when Shift is held. */
	if (ch >= 'A' && ch <= 'Z') {
		event->key.keysym.sym = ch + ('a' - 'A');
		event->key.keysym.mod = KMOD_LSHIFT;
	}
	if (ch >= 1 && ch <= 26 && ch != '\n' && ch != '\b' && ch != '\t') {
		event->key.keysym.sym = 'a' + ch - 1;
		event->key.keysym.mod = KMOD_LCTRL;
	}
	if (getenv("RVM_TEST_KEYPAD")) {
		const SDL_Keycode digits[] = { SDLK_KP_0, SDLK_KP_1, SDLK_KP_2,
		    SDLK_KP_3, SDLK_KP_4, SDLK_KP_5, SDLK_KP_6, SDLK_KP_7,
		    SDLK_KP_8, SDLK_KP_9 };
		SDL_Keycode keypad = 0;
		if (ch >= '0' && ch <= '9') keypad = digits[ch - '0'];
		switch (ch) {
		case '/': keypad = SDLK_KP_DIVIDE; break;
		case '*': keypad = SDLK_KP_MULTIPLY; break;
		case '-': keypad = SDLK_KP_MINUS; break;
		case '+': keypad = SDLK_KP_PLUS; break;
		case '=': keypad = SDLK_KP_EQUALS; break;
		case ',': keypad = SDLK_KP_COMMA; break;
		case '.': keypad = SDLK_KP_PERIOD; break;
		case '\n': keypad = SDLK_KP_ENTER; break;
		case '\b': keypad = SDLK_KP_BACKSPACE; break;
		}
		if (keypad) {
			event->key.keysym.sym = keypad;
			event->key.keysym.mod = KMOD_NUM;
		}
	}
	event->key.keysym.scancode = (SDL_Scancode)ch;
	previous = ch;
	release = !release;
	return 1;
}

/* Capture real rendered pixels before present. Requests are test-owned files
 * containing an output PPM path. Wait a few GUI frames for queued MMIO output. */
void
SDL_RenderPresent(SDL_Renderer *renderer)
{
	static void (*real_present)(SDL_Renderer *);
	static char pending[1024];
	static unsigned frames;
	const char *request = getenv("RVM_TEST_CAPTURE");
	if (real_present == NULL) real_present = dlsym(RTLD_NEXT, "SDL_RenderPresent");
	if (request != NULL && pending[0] == 0) {
		FILE *fp = fopen(request, "r");
		if (fp) {
			if (fgets(pending, sizeof(pending), fp)) {
				pending[strcspn(pending, "\r\n")] = 0;
				frames = 4;
			}
			fclose(fp);
		}
	}
	if (pending[0] && frames-- == 0) {
		int width, height;
		if (SDL_GetRendererOutputSize(renderer, &width, &height) == 0) {
			uint32_t *pixels = malloc((size_t)width * height * 4);
			if (pixels && SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
			    pixels, width * 4) == 0) {
				char temporary[1100];
				snprintf(temporary, sizeof(temporary), "%s.tmp", pending);
				FILE *fp = fopen(temporary, "wb");
				if (fp) {
					fprintf(fp, "P6\n%d %d\n255\n", width, height);
					for (int i = 0; i < width * height; i++) {
						unsigned char rgb[3] = { pixels[i] >> 16, pixels[i] >> 8, pixels[i] };
						fwrite(rgb, 1, 3, fp);
					}
					fclose(fp);
					unlink(request);
					rename(temporary, pending);
				}
			}
			free(pixels);
		}
		pending[0] = 0;
	}
	real_present(renderer);
}
