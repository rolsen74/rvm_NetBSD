/* A separate translation unit must participate in the global priority order. */
extern void constructor_step(int, int);
extern void constructor_record(char);
__attribute__((constructor(200))) static void middle(void) { constructor_step(2, 3); }
__attribute__((destructor(200))) static void middle_fini(void) { constructor_record('B'); }
