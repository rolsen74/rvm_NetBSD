/* Shared constants for the EARLY resident user-program fixture. */
#ifndef _RVM_PROCESS_TEST_H_
#define _RVM_PROCESS_TEST_H_
#define RVM_PROCESS_MESSAGE "Hello from a NetBSD/RVM user process via write(2)!"
/* Includes the newline emitted by process_program.S. */
#define RVM_PROCESS_MESSAGE_SIZE 51
#define RVM_PROCESS_SIGNAL 30
#endif
