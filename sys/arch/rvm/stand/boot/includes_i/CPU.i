; CPU constants retained from SnakeOS; OS-specific structures omitted.

**
** Copyright (c) 2024-2026 by Rene W. Olsen
** All rights reserved.
**
**


****************************************************


****************************************************

	IFND INCLUDES_CPU_I
INCLUDES_CPU_I: SET 1

; -- Cache Levels (Mask)
CACHE_LEVEL_1:		equ	$01
CACHE_LEVEL_MASK:	equ	$01

; -- Cache Types (Mask)
CACHE_TYPE_DATA:	equ	$01
CACHE_TYPE_INST:	equ	$02
CACHE_TYPE_MASK:	equ	$03

; --------------------------

VBR_00_Illegal:		equ	0
VBR_01_Privilege:	equ	1
VBR_02_MMU_Exe:		equ	2
VBR_03_MMU_Read:	equ	3
VBR_04_MMU_Write:	equ	4
VBR_05_MMU_Enable:	equ	5
VBR_06_MMU_Tabel:	equ	6
VBR_07_Mem_Read:	equ	7
VBR_08_Mem_Write:	equ	8
VBR_09_Div_Error:	equ	9
VBR_10_SysCall:		equ	10
VBR_11_Stack:		equ	11
VBR_LAST:		equ	12

; --------------------------

CRF_DataCache		equ	1		; Enable Read+Write Data Memory
CRF_InstCache		equ	2		; Enable Read Instuction Memory
CRF_Interrupt		equ	4		; Enable Interrupts
CRF_MMU			equ	8		; Enable MMU

****************************************************



	ENDC ; INCLUDES_CPU_I
