
**
** Copyright (c) 2024-2026 by Rene W. Olsen
** All rights reserved.
**
**

	IFND		INCLUDES_FHR_I
INCLUDES_FHR_I:		SET	1

****************************************************

FHR_RELTYPE_RELOC:	equ	0		; Exe : ABS 64 : Offset
FHR_RELTYPE_DEF:	equ	1		; Obj : ABS 64 : Define   : Offset + name (Lav en public label)
FHR_RELTYPE_REF:	equ	2		; Obj : ABS 64 : External : Offset + name (Brug en public label)

FHR_SIZE_32:		equ	0
FHR_SIZE_64:		equ	1

FHR_ENDIAN_Little:	equ	0
FHR_ENDIAN_Big:		equ	1

FHR_TYPE_Exe:		equ	0		; Exe File
FHR_TYPE_Object:	equ	1		; Object File (vbcc)
FHR_TYPE_Shared:	equ	2		; Shared Library File

FHR_PACK_None:		equ	0		; File is not packed
FHR_PACK_ZLib:		equ	1		; zlib
FHR_PACK_ZX0:		equ	2		; zx0

FHR_OS_RVM:		equ	$0100
FHR_OS_AmigaOS3:	equ	$0101
FHR_OS_AmigaOS4:	equ	$0102
FHR_OS_NetBSD:		equ	$0103

FHR_CPU_RVM:		equ	$0100
FHR_CPU_M68k:		equ	$0101
FHR_CPU_PowerPC:	equ	$0102

FHR_HEADER:		equ	$46485200	; FHR\0 - File Start (Header)
FHR_COD0:		equ	$434f4400	; COD\0 - Compacked format, for Exe's
FHR_COD1:		equ	$434f4401	; COD\1 - Fast format, for Object's
FHR_DAT0:		equ	$44415400	; DAT\0 - Compacked format, for Exe's
FHR_DAT1:		equ	$44415401	; DAT\1 - Fast format, for Object's
FHR_BSS0:		equ	$42535300	; BSS\0 - Compacked format, for Exe's
FHR_BSS1:		equ	$42535301	; BSS\1 - Fast format, for Object's
FHR_END0:		equ	$454e4400	; END\0 - Section End, For CODx,DATx,BSSx sections
FHR_REL0:		equ	$52454c00	; REL\0 - Reloc's
FHR_SYM0:		equ	$53594d00	; SYM\0 - Symbol Names

 STRUCTURE	FHR_Header,0
 U32		FHR_ID			; Should be FHR_HEADER
 U8		FHR_Size		; 0 = 32 bits, 1 = 64 bits
 U8		FHR_Endian		; 0 = Little, 1 = Big
 U8		FHR_Type		; 0 = Exe, 1 = Object, 2 = Shared
 U8		FHR_Pack		; 0 = None, 1 = zlib
 U16		FHR_Pad2		;
 U16		FHR_OSType		; 0x0100 = RVM, 0x0101 = AmigaOS3, 0x0102 = AmigaOS4
 U16		FHR_CPUType		; 0x0100 = RVM, 0x0101 = m68k,     0x0102 = PowerPC
 U16		FHR_Sections		; Number of Sections
 LABEL		FHR_SIZEOF

 ;--- Section Info Start - One entry per section if unpacked
 U64		FHR_SecSize		; Memory Size ( xx Bytes )
 U16		FHR_SecAlign		; Memory Alignment ( 1 << x )
 U16		FHR_SecFlags		; Memory Flags ( SECFLAG_xx ) ;--- Only used if FHR_Packed or encrypted is non Zero
 LABEL		FHR_Data

 ;--- If FHR_Pack = ZLib, sizes follow the ordinary 16-byte header.
 ;--- The unpacked size includes the ordinary header.
 STRUCTURE	FHR_PackedSizes32,0
 U32		FHR_DataSize1_32	; Packed data size
 U32		FHR_DataSize2_32	; Full unpacked file size

 STRUCTURE	FHR_PackedSizes64,0
 U64		FHR_DataSize1_64	; Packed data size
 U64		FHR_DataSize2_64	; Full unpacked file size

 STRUCTURE	SECI_SecInfo,0
 U64		SECI_Size		; Memory Size ( xx Bytes )
 U16		SECI_Align		; Memory Alignment ( 1 << x )
 U16		SECI_Flags		; Memory Flags ( SECFLAG_xx ) ;--- Only used if FHR_Packed or encrypted is non Zero
 LABEL		SECI_SIZEOF		; 

 STRUCTURE	COD0_Header,0
 U32		COD0_ID			; FHR_COD0
 U64		COD0_Length		; Length [ ( ID + Length ) not included ]
 U16		COD0_CRC		; CRC
 LABEL		COD0_Data		; Data   [ SecInfo Size ]
 LABEL		COD0_Relocs		; Relocs [ COD0_Length - SecInfo Size - 2 (CRC) ]

 STRUCTURE	DAT0_Header,0
 U32		DAT0_ID			; FHR_DAT0
 U64		DAT0_Length		; Length [ ( ID + Length ) not included ]
 U16		DAT0_CRC		; CRC
 LABEL		DAT0_Data		; Data   [ SecInfo Size ]
 LABEL		DAT0_Relocs		; Relocs [ DAT0_Length - SecInfo Size - 2 (CRC) ]

****************************************************

	ENDC	; INCLUDES_FHR_I
