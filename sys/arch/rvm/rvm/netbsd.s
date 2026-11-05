
;
; Disassembled with VMDis v1.1 (14.08.2026)
;
; Size .. : 198 bytes
; Type .. : Executable
; Name .. : 'netbsd.fhr'
; MD5 ... : $a1c939032c4b80132202c6162f75e056
;

	Section		"SEC0",CODE
	SecAlign	1

Sec_0000:
	Jsr		L0002					; $00010000 005d 3700 0000 0000 0100 16
L0001:	Jmp		(L0001,PC)				; $0001000b 005e 3300 0000 0000 0000 00

L0002:	Pushf		r62					; $00010016 0111 4000 0000 0000 0000
	Move.q		#$0000000000000001,Sec_0002		; $00010020 0027 3801 3700 0000 0000 0300 00
	Move.q		#$0000000000000002,Sec_0002		; $0001002d 0027 3802 3700 0000 0000 0300 00
	Nop							; $0001003a 005c
	Popf							; $0001003c 0110
	Rts							; $0001003e 0060


	Section		"SEC1",DATA_RO
	SecAlign	1

Sec_0001:
	dc.l		$4e657442,$53442f52,$564d2065,$61726c79	; $00020000 
	dc.l		$20626f6f,$74737472			; $00020010 
	dc.w		$6170					; $00020018 
	dc.b		$00					; $0002001a 

	Section		"SEC2",BSS_RW
	SecAlign	1

Sec_0002:
	ds.b		8					; $00030000 
