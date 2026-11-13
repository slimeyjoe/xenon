; SPDX-License-Identifier: BSD-2-Clause
; VERTB interrupt server for sbprobe: a1 = is_Data -> ULONG counter.
; Servers return with Z set (d0 = 0) so the chain continues.
	section	code,code
	xdef	_vbserver
_vbserver:
	addq.l	#1,(a1)
	moveq	#0,d0
	rts
	end
