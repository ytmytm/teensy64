.export _load_and_run

calladdr   = $aa
endaddr    = $ac
loadaddr   = $ae

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
:       ;sta $0002,y
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

        lda #$01    ; set current file (emulate read from drive 8)
        ldx #$08
        tay
        jsr $ffba   ; SETLFS - set file parameters

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

loader:
        ror $d011               ; blank screen

        sei
        ldx #$00
        stx $c6                 ; clear keyboard buffer

; read here
; ffd5
; update aa/ab to call address
; update ac/ad to end address
; update ae/af to load address

	lda #0
	tax
	tay
	jsr $ffd5		; LOAD

        sec                     ; turn screen on
        rol $d011

	; A/X is the end address here
	lda $ac
	ldx $ad
        stx $2e                 ; update BASIC pointers
        stx $30                 ; (FIXME: Check if one of the ROM calls helps here)
        stx $32
        sta $2d
        sta $2f
        sta $31

        cli                     ; enable interrupts
        jsr $e453               ; restore vectors
        jmp (calladdr)
