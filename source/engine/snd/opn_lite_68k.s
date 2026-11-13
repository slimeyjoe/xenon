; SPDX-License-Identifier: BSD-2-Clause
; opn_lite's per-sample render loop for the 68040/68060 (vbcc, Amiga): the same arithmetic
; as lite_render() in opn_lite.c for the os == 1 case, bit for bit (probes/amiga: "opnbench
; sum" against opnsum), with the operator evaluation, the channel algorithms, the envelope-
; clock and SSG entry checks, the DC blocker and the clamp written out so that nothing is
; spilled and nothing is called per sample. The envelope visits (opn_lite_eg_due) and the
; SSG (opn_lite_ssg_output) stay in C and are called only when due / audible.
;
;   void opn_lite_render_68k(__reg("a0") struct opn_core *c, __reg("a1") int16_t *mono,
;                            __reg("d0") int frames)
;
; Tuned for the 68040 (MC68040UM section 10): (d16,An) operands cost 1 clock where indexed
; ones cost 3, so only the two table lookups per operator are indexed; shifts/rotates by an
; immediate cost 2 and by a register 3; a not-taken branch costs 3, a taken one 2; MULS.L 20
; (the DC blocker's one multiply per sample). The 68060 pairs most of these in one clock.
; Operator trick: wave[] keeps the sign in bit 15 and it is NOT masked off before adding the
; attenuation: it only adds 128 to the shift count, which LSR takes modulo 64. (Immediate
; shift counts stop at 8, so the 10-bit index in bits 18..27 costs SWAP + LSR.W #2.)
;
; Structure offsets are vbcc's (2-byte alignment); opn_lite.c checks them at compile time
; when XE_OPN_ASM_68K is defined, so a layout change fails the build instead of the music.
	machine	68040
	section	code,code

; struct opn_core
C_FSTEP		equ	4
C_EG_ACC	equ	24
C_EGC		equ	28
C_EG_NEXT	equ	32
C_CH		equ	294
C_NOISE_NEXT	equ	698
C_NOISE_HALF	equ	702
C_SSG_ON	equ	730
C_SSG_IDLE	equ	734
C_HP_X1		equ	738
C_HP_Y1		equ	742
C_HP_K		equ	746
; struct chan
CH_SIZE		equ	120
CH_FB1		equ	104
CH_FB2		equ	108
CH_ACTIVE	equ	116
CH_QUIET	equ	117
CH_ALG		equ	118
CH_FB		equ	119
; struct op (op k at k * OP_SIZE)
OP_SIZE		equ	26
OP_PHASE	equ	0
OP_STEP		equ	4
OP_ENV		equ	12
OP_ATT		equ	14
EG_QUIET	equ	$380
EG_CLOCK	equ	$30000		; EG_PER_FM << 16

	xref	_opn_lite_wave		; uint16_t[1024]: 4.8 log attenuation | sign << 15
	xref	_opn_lite_pow		; uint16_t[256]
	xref	_opn_lite_eg_due	; void (struct opn_core *c)
	xref	_opn_lite_ssg_output	; int32_t (struct opn_core *c)
	xdef	_opn_lite_render_68k

; OPV op_offset, mod_reg, out_reg: 14-bit signed operator output into out_reg (long), as
; OP_VOLUME(): 0 above EG_QUIET, else sign * (pow[(s + att) & 255] >> ((s + att) >> 8)) with
; s = wave[((phase >> 18) + mod) & 1023]. Uses d0/d1; mod_reg is consumed before out_reg is
; written, so the two may be the same register. a0 = channel, a4 = wave, a5 = pow.
OPV	macro
	move.l	\1+OP_PHASE(a0),d0
	swap	d0
	lsr.w	#2,d0
	add.w	\2,d0
	moveq	#0,\3
	cmp.w	#EG_QUIET,\1+OP_ENV(a0)
	bhi.s	.q\@
	and.w	#$3ff,d0
	move.w	(a4,d0.w*2),d1
	move.w	d1,d0
	add.w	\1+OP_ATT(a0),d0
	move.b	d0,\3
	move.w	(a5,\3.w*2),\3
	lsr.w	#8,d0
	lsr.l	d0,\3
	tst.w	d1
	bpl.s	.q\@
	neg.l	\3
.q\@
	endm

; OPV0 op_offset, out_reg: the same without modulation (out_reg is not d0/d1)
OPV0	macro
	moveq	#0,\2
	cmp.w	#EG_QUIET,\1+OP_ENV(a0)
	bhi.s	.q\@
	move.l	\1+OP_PHASE(a0),d0
	swap	d0
	lsr.w	#2,d0
	and.w	#$3ff,d0
	move.w	(a4,d0.w*2),d1
	move.w	d1,d0
	add.w	\1+OP_ATT(a0),d0
	move.b	d0,\2
	move.w	(a5,\2.w*2),\2
	lsr.w	#8,d0
	lsr.l	d0,\2
	tst.w	d1
	bpl.s	.q\@
	neg.l	\2
.q\@
	endm

; HALF src, dst: dst = src >> 1 (the modulation of the next operator)
HALF	macro
	move.l	\1,\2
	asr.l	#1,\2
	endm

_opn_lite_render_68k:
	movem.l	d2-d7/a2-a6,-(a7)
	move.l	a0,a2			; c
	move.l	a1,a3			; mono
	move.l	d0,d7			; frames
	ble	.done
	lea	_opn_lite_wave,a4
	lea	_opn_lite_pow,a5
.sample
	; envelope clock: eg_acc += fstep; clocks due while eg_acc >= 3 << 16
	move.l	C_FSTEP(a2),d0
	add.l	d0,C_EG_ACC(a2)
	cmp.l	#EG_CLOCK,C_EG_ACC(a2)
	bcs.s	.noeg
	bsr	.eg
.noeg
	; the three FM channels
	moveq	#0,d6
	lea	C_CH(a2),a0
	tst.b	CH_ACTIVE(a0)
	beq.s	.c1
	bsr	.chan
	add.l	d5,d6
.c1	lea	CH_SIZE(a0),a0
	tst.b	CH_ACTIVE(a0)
	beq.s	.c2
	bsr	.chan
	add.l	d5,d6
.c2	lea	CH_SIZE(a0),a0
	tst.b	CH_ACTIVE(a0)
	beq.s	.ssg
	bsr	.chan
	add.l	d5,d6
.ssg
	; SSG: computed only while a channel has an amplitude, else parked (SSG_OUT)
	tst.l	C_SSG_ON(a2)
	beq.s	.park
	move.l	a2,-(a7)
	jsr	_opn_lite_ssg_output
	addq.w	#4,a7
	asr.l	#2,d0
	add.l	d0,d6
	bra.s	.mix
.park
	move.l	C_NOISE_HALF(a2),C_NOISE_NEXT(a2)
	addq.l	#1,C_SSG_IDLE(a2)
.mix
	; x = (fm + ssg / 4) * 2; y = ((x - x1) << 8) + y1 - (((y1 >> 8) * k) >> 8)
	add.l	d6,d6
	move.l	d6,d0
	sub.l	C_HP_X1(a2),d0
	lsl.l	#8,d0
	move.l	C_HP_Y1(a2),d1
	add.l	d1,d0
	asr.l	#8,d1
	muls.l	C_HP_K(a2),d1
	asr.l	#8,d1
	sub.l	d1,d0
	move.l	d6,C_HP_X1(a2)
	move.l	d0,C_HP_Y1(a2)
	asr.l	#8,d0
	cmp.l	#32767,d0
	ble.s	.nohi
	move.l	#32767,d0
.nohi	cmp.l	#-32768,d0
	bge.s	.nolo
	move.l	#-32768,d0
.nolo	move.w	d0,(a3)+
	subq.l	#1,d7
	bne	.sample
.done
	movem.l	(a7)+,d2-d7/a2-a6
	rts

; envelope clocks due in this step (step_envelopes): egc advances per clock, the channels
; are visited (C) only when the earliest due one is reached; eg_next is ~0 while no channel
; is active, so no "any" test is needed
.eg
	sub.l	#EG_CLOCK,C_EG_ACC(a2)
	addq.l	#1,C_EGC(a2)
	move.l	C_EGC(a2),d0
	cmp.l	C_EG_NEXT(a2),d0
	bcs.s	.egnext
	move.l	a2,-(a7)
	jsr	_opn_lite_eg_due
	addq.w	#4,a7
.egnext
	cmp.l	#EG_CLOCK,C_EG_ACC(a2)
	bcc.s	.eg
	rts

; one channel sample (chan_output): a0 = channel, result in d5; uses d0-d5
.chan
	move.l	0*OP_SIZE+OP_STEP(a0),d0
	add.l	d0,0*OP_SIZE+OP_PHASE(a0)
	move.l	1*OP_SIZE+OP_STEP(a0),d0
	add.l	d0,1*OP_SIZE+OP_PHASE(a0)
	move.l	2*OP_SIZE+OP_STEP(a0),d0
	add.l	d0,2*OP_SIZE+OP_PHASE(a0)
	move.l	3*OP_SIZE+OP_STEP(a0),d0
	add.l	d0,3*OP_SIZE+OP_PHASE(a0)
	; op1 with feedback: m = fb ? (fb1 + fb2) >> (10 - fb) : 0
	moveq	#0,d5
	move.b	CH_FB(a0),d1
	beq.s	.nofb
	moveq	#10,d0
	sub.b	d1,d0
	move.l	CH_FB1(a0),d5
	add.l	CH_FB2(a0),d5
	asr.l	d0,d5
.nofb
	OPV	0*OP_SIZE,d5,d2		; o1
	move.l	CH_FB1(a0),CH_FB2(a0)
	move.l	d2,CH_FB1(a0)
	tst.b	CH_QUIET(a0)
	bne.s	.quiet
	moveq	#0,d0
	move.b	CH_ALG(a0),d0
	move.w	(.jt,pc,d0.w*2),d0
	jmp	(.jt,pc,d0.w)
.jt	dc.w	.alg0-.jt,.alg1-.jt,.alg2-.jt,.alg3-.jt
	dc.w	.alg4-.jt,.alg5-.jt,.alg6-.jt,.alg7-.jt
.quiet	moveq	#0,d5
	rts

.alg0	HALF	d2,d5
	OPV	1*OP_SIZE,d5,d3		; o2 <- o1
	HALF	d3,d5
	OPV	2*OP_SIZE,d5,d4		; o3 <- o2
	HALF	d4,d5
	OPV	3*OP_SIZE,d5,d5		; o4 <- o3
	rts
.alg1	OPV0	1*OP_SIZE,d3		; o2
	move.l	d2,d5
	add.l	d3,d5
	asr.l	#1,d5
	OPV	2*OP_SIZE,d5,d4		; o3 <- o1 + o2
	HALF	d4,d5
	OPV	3*OP_SIZE,d5,d5		; o4 <- o3
	rts
.alg2	OPV0	1*OP_SIZE,d3		; o2
	HALF	d3,d5
	OPV	2*OP_SIZE,d5,d4		; o3 <- o2
	move.l	d2,d5
	add.l	d4,d5
	asr.l	#1,d5
	OPV	3*OP_SIZE,d5,d5		; o4 <- o1 + o3
	rts
.alg3	HALF	d2,d5
	OPV	1*OP_SIZE,d5,d3		; o2 <- o1
	OPV0	2*OP_SIZE,d4		; o3
	move.l	d3,d5
	add.l	d4,d5
	asr.l	#1,d5
	OPV	3*OP_SIZE,d5,d5		; o4 <- o2 + o3
	rts
.alg4	HALF	d2,d5
	OPV	1*OP_SIZE,d5,d3		; o2 <- o1
	OPV0	2*OP_SIZE,d4		; o3
	HALF	d4,d5
	OPV	3*OP_SIZE,d5,d5		; o4 <- o3
	add.l	d3,d5			; o2 + o4
	rts
.alg5	HALF	d2,d5
	OPV	1*OP_SIZE,d5,d3		; o2 <- o1
	OPV	2*OP_SIZE,d5,d4		; o3 <- o1
	OPV	3*OP_SIZE,d5,d5		; o4 <- o1
	add.l	d3,d5
	add.l	d4,d5			; o2 + o3 + o4
	rts
.alg6	HALF	d2,d5
	OPV	1*OP_SIZE,d5,d3		; o2 <- o1
	OPV0	2*OP_SIZE,d4		; o3
	OPV0	3*OP_SIZE,d5		; o4
	add.l	d3,d5
	add.l	d4,d5			; o2 + o3 + o4
	rts
.alg7	OPV0	1*OP_SIZE,d3		; o2
	OPV0	2*OP_SIZE,d4		; o3
	OPV0	3*OP_SIZE,d5		; o4
	add.l	d2,d5
	add.l	d3,d5
	add.l	d4,d5			; o1 + o2 + o3 + o4
	rts
	end
