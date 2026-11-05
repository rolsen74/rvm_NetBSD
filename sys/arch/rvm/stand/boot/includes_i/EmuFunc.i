
**
** Copyright (c) 2024-2026 by Rene W. Olsen
** All rights reserved.
**
**

	IFND		INCLUDES_EMUFUNC_I
INCLUDES_EMUFUNC_I:		SET	1

****************************************************

	include "Types.i"

****************************************************

EMUCMD_PrintString:	equ	2	; Shell
EMUCMD_PrintChar:	equ	3 	; Shell
EMUCMD_Open:		equ	4	; File ID
EMUCMD_Close:		equ	5	; File ID
EMUCMD_Seek:		equ	6	; File ID
EMUCMD_Read:		equ	7	; File ID
EMUCMD_Write:		equ	8	; File ID
EMUCMD_Delete:		equ	9	; Path
EMUCMD_Examine:		equ	10	; Path
EMUCMD_ExamineID:	equ	11	; File ID
EMUCMD_LockDir:		equ	12	; Dir ID
EMUCMD_UnlockDir:	equ	13	; Dir ID
EMUCMD_MakeDir:		equ	14	; Dir ID
EMUCMD_ChangeDir:	equ	15	; Dir ID

****************************************************

; Error Codes:
EMUERR_Okay:		equ	1	; Okay
EMUERR_Error:		equ	-1	; Generic Error
EMUERR_NotFound:	equ	-2	; File or Directory not found
EMUERR_InvalidID:	equ	-3	; Invalid File or Directory ID
EMUERR_EOF:		equ	-4	; End of File

****************************************************

****************************************************

	ENDC	; INCLUDES_EMUFUNC_I
