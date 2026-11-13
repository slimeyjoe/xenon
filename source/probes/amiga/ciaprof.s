; SPDX-License-Identifier: BSD-2-Clause
; PC-sampling interrupt code for opnprof.c: a CIA timer (or VERTB) interrupt vector whose
; is_Data (a1) points at a struct profdata. The interrupted context's exception frame lies
; on the supervisor stack above Exec's saved registers; it is found by its format/vector
; word (format 0: $0068 for a level-2 autovector, $006C level 3, $0078 level 6) followed by a
; plausible status register (user mode, or supervisor at interrupt mask 0/1), and the PC in
; it is counted in a histogram of one ULONG per two bytes of the profiled code range.
;
; struct profdata {           offset
;     ULONG *hist;            0
;     ULONG  base;            4   first address of the profiled range
;     ULONG  len;             8   its length in bytes
;     ULONG  total;          12   samples taken
;     ULONG  outside;        16   user-mode PC outside the range
;     ULONG  super;          20   interrupted supervisor code (Exec, another interrupt)
;     ULONG  nomatch;        24   no frame found within the scan window
;     UWORD  vecoff;         28   format/vector word to look for
;     UWORD  pad;            30
;     ULONG  outpc[16];      32   ring of the last outside PCs
;     ULONG  outpos;         96
; };
; Clobbers d0/d1/a0 only (saves d2/d3); returns with Z set like a VERTB server.
	machine	68020
	section	code,code
	xdef	_profsample
_profsample:
	movem.l	d2/d3,-(a7)
	addq.l	#1,12(a1)		; total
	lea	8(a7),a0		; above our saved registers
	move.w	28(a1),d1		; format/vector word
	moveq	#95,d0			; scan 96 words of stack
.scan:
	cmp.w	(a0)+,d1
	bne.s	.cont
	move.w	-8(a0),d2		; the candidate frame's SR
	lsr.w	#8,d2
	beq.s	.user			; T1 T0 S M clear, mask 0: the task
	cmp.w	#$20,d2
	beq.s	.super
	cmp.w	#$21,d2
	beq.s	.super
.cont:
	dbf	d0,.scan
	addq.l	#1,24(a1)		; nomatch
	bra.s	.done
.super:
	addq.l	#1,20(a1)
	bra.s	.done
.user:
	move.l	-6(a0),d2		; the frame's PC (word aligned)
	move.l	d2,d3
	sub.l	4(a1),d2
	cmp.l	8(a1),d2
	bcc.s	.outside
	lsr.l	#1,d2
	move.l	(a1),a0
	addq.l	#1,(a0,d2.l*4)
	bra.s	.done
.outside:
	addq.l	#1,16(a1)
	move.l	96(a1),d0
	lea	32(a1),a0
	move.l	d3,(a0,d0.l*4)
	addq.l	#1,d0
	and.l	#15,d0
	move.l	d0,96(a1)
.done:
	movem.l	(a7)+,d2/d3
	moveq	#0,d0
	rts
	end
