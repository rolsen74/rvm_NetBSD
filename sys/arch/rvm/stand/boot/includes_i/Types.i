
**
** Copyright (c) 2024-2026 by Rene W. Olsen
** All rights reserved.
**
**

	IFND		INCLUDES_TYPES_I
INCLUDES_TYPES_I:	SET	1

**[ Types ]*****************************************

STRUCTURE:	MACRO		; structure name, initial offset
\1:		EQU		0
SOFFSET:	SET		\2
		ENDM

LABEL:		MACRO		; Define a label
\1:		EQU		SOFFSET
		ENDM

PTR:		MACRO		; Pointer
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+8
		ENDM

STR:		MACRO		; String Pointer
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+8
		ENDM

U8:		MACRO		; u8 (Unsigned 8 bits)
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+1
		ENDM

S8:		MACRO		; s8 (Signed 8 bits)
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+1
		ENDM

U16:		MACRO		; u16 (Unsigned 16 bits)
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+2
		ENDM

S16:		MACRO		; s16 (Signed 16 bits)
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+2
		ENDM

U32:		MACRO		; u32 (Unsigned 32 bits)
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+4
		ENDM

S32:		MACRO		; s32 (Signed 32 bits)
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+4
		ENDM

U64:		MACRO		; u64 (Unsigned 64 bits)
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+8
		ENDM

S64:		MACRO		; s64 (Signed 64 bits)
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+8
		ENDM

ARRAY:		MACRO		; ds.b label,size,count
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+(\2*\3)
		ENDM

STRUCT:		MACRO		; ds.b label,size
\1:		EQU		SOFFSET
SOFFSET:	SET		SOFFSET+\2
		ENDM

****************************************************

	ENDC	; INCLUDES_TYPES_I
