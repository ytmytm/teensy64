.export _load_and_run

calladdr   = $aa
loadaddr   = $ac
endaddr    = $ae

; =============================================================================
; void load_and_run();
;
; System init based on http://codebase64.org/doku.php?id=base:kernalbasicinit
; and https://github.com/KimJorgensen/easyflash/blob/master/3rdParty/EasyLoader
; /loader/file.asm
; =============================================================================
_load_and_run:
        sei
        cld
        ldx #$ff
        txs
        jsr $ff84    ; IOINIT - Initialize I/O

        ; initialize SID registers (not done by Kernal reset routine)
        ldx #$17
        lda #$00
:       sta $d400,x
        dex
        bpl :-

        ; initialize system constants (RAMTAS without RAM-test)
        lda #$00
        tay
:       ;sta $0002,y	; don't clear zeropage - there are LOAD parameters there
        sta $0200,y
        sta $023c,y     ; don't clear the datasette buffer
        iny
        bne :-

        ldx #$3c
        ldy #$03
        stx $b2
        sty $b3

        ldx #$00
        ldy #$a0
        jsr $fd8c

        jsr $ff8a    ; RESTOR - restore kernal vectors
        jsr $ff81    ; CINT - initialize screen editor

; =============================================================================
        lda $324
        sta restore_lower+1
        lda $325
        sta restore_upper+1

        lda #<chrin_trap    ; install our CHRIN handler
        sta $324
        lda #>chrin_trap
        sta $325

        lda #$01    ; set current file (emulate read from tape)
        ldx #$01
        tay			; sa<>0 so file header info is used
        jsr $ffba   ; SETLFS - set file parameters

		ldx #0
:		lda loader,x
		sta $0351,x
		inx
		cpx #(loaderend-loader+1)
		bne :-

        jmp $fcfe   ; Start BASIC

; =============================================================================
chrin_trap:
        sei
        ldx #$fa
        txs

restore_lower:
        lda #$00
        sta $324
restore_upper:
        lda #$00
        sta $325
		jmp $0351

		;; only fully relocatable code below
loader:
        sei
        ldx #$00
        stx $c6                 ; clear keyboard buffer

		lda #0
		sta calladdr
		sta calladdr+1			; reset call address, Teensy will update it if load address<>$0801
		tax
		tay
		jsr $ffd5				; LOAD
		stx endaddr				; end address
		sty endaddr+1

; update aa/ab to call address (if == 0 then do BASIC RUN)
; update ac/ad to load address
; update ae/af to end address (through x/y)

		; X/Y is the end address here
        sty $2e                 ; update BASIC pointers
        sty $30                 ; (FIXME: Check if one of the ROM calls helps here)
        sty $32
        stx $2d
        stx $2f
        stx $31

        cli                     ; enable interrupts
        jsr $e453               ; restore vectors
		;
		lda calladdr
		ora calladdr+1
		bne :+					; start ML program
		jsr $a659    			; or set basic pointer and CLR
		jmp $a7ae    			; and RUN
:        jmp (calladdr)			; start ML program if load address <> $0801
loaderend:
