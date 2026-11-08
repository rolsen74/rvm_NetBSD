/* Native RVM nonlocal jumps. Slots are eight bytes; sigset_t occupies 16.
 * Save the caller's stack beyond the return address and all preserved GPRs.
 */
#define _LOCORE
#include <sys/syscall.h>
        .text
        .globl _setjmp
        .globl __setjmp14
        .globl __sigsetjmp14
_setjmp:
        clr.q r3
        jmp jmp_save
__setjmp14:
        move.q #1,r3
__sigsetjmp14:
jmp_save:
        move.q r10,(0,r2)
        move.q r11,(8,r2)
        move.q r12,(16,r2)
        move.q r13,(24,r2)
        move.q r14,(32,r2)
        move.q r15,(40,r2)
        move.q r16,(48,r2)
        move.q r17,(56,r2)
        move.q r18,(64,r2)
        move.q r19,(72,r2)
        move.q r20,(80,r2)
        move.q r21,(88,r2)
        move.q r22,(96,r2)
        move.q r23,(104,r2)
        move.q r24,(112,r2)
        move.q r25,(120,r2)
        move.q r26,(128,r2)
        move.q r27,(136,r2)
        move.q r28,(144,r2)
        move.q r29,(152,r2)
        move.q r30,(160,r2)
        move.q r31,(168,r2)
        move.q r32,(176,r2)
        move.q r33,(184,r2)
        move.q r34,(192,r2)
        move.q r35,(200,r2)
        move.q r36,(208,r2)
        move.q r37,(216,r2)
        move.q r38,(224,r2)
        move.q r39,(232,r2)
        move.q r40,(240,r2)
        move.q r41,(248,r2)
        move.q r42,(256,r2)
        move.q r43,(264,r2)
        move.q r44,(272,r2)
        move.q r45,(280,r2)
        move.q r46,(288,r2)
        move.q r47,(296,r2)
        move.q r48,(304,r2)
        move.q r49,(312,r2)
        move.q r50,(320,r2)
        move.q r51,(328,r2)
        move.q r52,(336,r2)
        move.q r53,(344,r2)
        move.q r54,(352,r2)
        move.q r55,(360,r2)
        move.q r56,(368,r2)
        move.q r57,(376,r2)
        move.q r58,(384,r2)
        move.q r59,(392,r2)
        move.q r60,(400,r2)
        move.q r61,(408,r2)
        move.q r62,(416,r2)
        lea (8,r63),r1
        move.q r1,(424,r2)
        move.q (r63),r1
        move.q r1,(432,r2)
        move.q #0x52564d4a4d503031,r1
        move.q r1,(464,r2)
        move.q r3,(440,r2)
        /* Save all FP registers and their control/status in the same buffer. */
        lea (480,r2),r4
        .short 0x01c3
        .byte 4
        .short 0x0069,12
        .byte 0
        .short 0x0069,13
        .byte 1
        move.q r0,(992,r2)
        move.q r1,(1000,r2)
        tst.l r3
        beq jmp_saved
        move.q r2,r11
        lea (448,r2),r4
        clr.q r3
        clr.q r2
        move.q #SYS___sigprocmask14,r0
        syscall
        bcs jmp_invalid
        move.q (8,r11),r11
jmp_saved:
        clr.q r0
        rts
        .globl _longjmp
        .globl __longjmp14
        .globl __siglongjmp14
_longjmp:
__longjmp14:
__siglongjmp14:
        move.q (464,r2),r1
        cmp.q #0x52564d4a4d503031,r1
        bne jmp_invalid
        move.q r2,r11
        move.l r3,r12
        tst.l r12
        bne jmp_nonzero
        move.q #1,r12
jmp_nonzero:
        move.q (440,r11),r1
        tst.l r1
        beq jmp_restore
        lea (448,r11),r3
        move.q #3,r2
        clr.q r4
        move.q #SYS___sigprocmask14,r0
        syscall
        bcs jmp_invalid
jmp_restore:
        lea (480,r11),r2
        .short 0x01c1
        .byte 2
        move.q (992,r11),r0
        .short 0x0068
        .byte 0
        .short 12
        move.q (1000,r11),r0
        .short 0x0068
        .byte 0
        .short 13
        move.l r12,r0
        move.q (432,r11),r1
        move.q (424,r11),r63
        move.q r11,r2
        move.q (0,r2),r10
        move.q (8,r2),r11
        move.q (16,r2),r12
        move.q (24,r2),r13
        move.q (32,r2),r14
        move.q (40,r2),r15
        move.q (48,r2),r16
        move.q (56,r2),r17
        move.q (64,r2),r18
        move.q (72,r2),r19
        move.q (80,r2),r20
        move.q (88,r2),r21
        move.q (96,r2),r22
        move.q (104,r2),r23
        move.q (112,r2),r24
        move.q (120,r2),r25
        move.q (128,r2),r26
        move.q (136,r2),r27
        move.q (144,r2),r28
        move.q (152,r2),r29
        move.q (160,r2),r30
        move.q (168,r2),r31
        move.q (176,r2),r32
        move.q (184,r2),r33
        move.q (192,r2),r34
        move.q (200,r2),r35
        move.q (208,r2),r36
        move.q (216,r2),r37
        move.q (224,r2),r38
        move.q (232,r2),r39
        move.q (240,r2),r40
        move.q (248,r2),r41
        move.q (256,r2),r42
        move.q (264,r2),r43
        move.q (272,r2),r44
        move.q (280,r2),r45
        move.q (288,r2),r46
        move.q (296,r2),r47
        move.q (304,r2),r48
        move.q (312,r2),r49
        move.q (320,r2),r50
        move.q (328,r2),r51
        move.q (336,r2),r52
        move.q (344,r2),r53
        move.q (352,r2),r54
        move.q (360,r2),r55
        move.q (368,r2),r56
        move.q (376,r2),r57
        move.q (384,r2),r58
        move.q (392,r2),r59
        move.q (400,r2),r60
        move.q (408,r2),r61
        move.q (416,r2),r62
        jmp (r1)
jmp_invalid:
        jmp abort
