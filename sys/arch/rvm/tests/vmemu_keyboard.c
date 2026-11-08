/* Compile the real VMEmu converter; unused emulator functions are discarded
 * with -ffunction-sections -Wl,--gc-sections. */
#include <assert.h>
#include "gui_SDL2/GUI_Win_Keyboard.c"
int main(void)
{
	for (int ch = 'a'; ch <= 'z'; ch++) {
		assert(__Convert_SDL_Key(ch, KMOD_NONE) == ch);
		assert(__Convert_SDL_Key(ch, KMOD_LSHIFT) == ch - 32);
		assert(__Convert_SDL_Key(ch, KMOD_RSHIFT) == ch - 32);
		assert(__Convert_SDL_Key(ch, KMOD_CAPS) == ch - 32);
		assert(__Convert_SDL_Key(ch, KMOD_SHIFT | KMOD_CAPS) == ch);
		assert(__Convert_SDL_Key(ch, KMOD_CTRL) == ch - 'a' + 1);
	}
	const char *plain = "1234567890-=[]\\;',./`";
	const char *shifted = "!@#$%^&*()_+{}|:\"<>?~";
	for (unsigned i = 0; plain[i]; i++)
		assert(__Convert_SDL_Key(plain[i], KMOD_SHIFT) == shifted[i]);
	assert(__Convert_SDL_Key(SDLK_BACKSPACE, KMOD_NONE) == '\b');
	assert(__Convert_SDL_Key(SDLK_RETURN, KMOD_SHIFT) == '\n');
	assert(__Convert_SDL_Key(SDLK_UP, KMOD_NONE) == KEY_UP);
	assert(__Convert_SDL_Key(SDLK_LSHIFT, KMOD_SHIFT) == KEY_NONE);
	const SDL_Keycode digits[] = { SDLK_KP_0, SDLK_KP_1, SDLK_KP_2,
	    SDLK_KP_3, SDLK_KP_4, SDLK_KP_5, SDLK_KP_6, SDLK_KP_7,
	    SDLK_KP_8, SDLK_KP_9 };
	const U16 navigation[] = { KEY_INSERT, KEY_END, KEY_DOWN, KEY_PAGEDOWN,
	    KEY_LEFT, KEY_NONE, KEY_RIGHT, KEY_HOME, KEY_UP, KEY_PAGEUP };
	for (unsigned i = 0; i < 10; i++) {
		assert(__Convert_SDL_Key(digits[i], KMOD_NUM) == '0' + i);
		assert(__Convert_SDL_Key(digits[i], KMOD_NONE) == navigation[i]);
	}
	const SDL_Keycode operators[] = { SDLK_KP_DIVIDE, SDLK_KP_MULTIPLY,
	    SDLK_KP_MINUS, SDLK_KP_PLUS, SDLK_KP_EQUALS, SDLK_KP_EQUALSAS400,
	    SDLK_KP_ENTER, SDLK_KP_TAB, SDLK_KP_BACKSPACE, SDLK_KP_COMMA };
	const char expected[] = "/*-+==\n\t\b,";
	for (unsigned i = 0; i < sizeof(operators)/sizeof(operators[0]); i++) {
		assert(__Convert_SDL_Key(operators[i], KMOD_NONE) == expected[i]);
		assert(__Convert_SDL_Key(operators[i], KMOD_NUM | KMOD_SHIFT) == expected[i]);
	}
	assert(__Convert_SDL_Key(SDLK_KP_PERIOD, KMOD_NUM) == '.');
	assert(__Convert_SDL_Key(SDLK_KP_PERIOD, KMOD_NONE) == KEY_DELETE);
	assert(__Convert_SDL_Key(SDLK_NUMLOCKCLEAR, KMOD_NUM) == KEY_NONE);
	puts("PASS: VMEmu modifiers, punctuation, Ctrl, keypad digits/operators/navigation and special keys");
	return 0;
}
