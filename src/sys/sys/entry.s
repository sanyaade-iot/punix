/*
 * Punix
 * Copyright (C) 2003 PpHd
 * Copyright 2004, 2005 Chris Williams
 * 
 * $Id$
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

.global buserr
.global SPURIOUS
.global ADDRESS_ERROR
.global ILLEGAL_INSTR
.global ZERO_DIVIDE
.global CHK_INSTR
.global I_TRAPV
.global PRIVILEGE
.global TRACE
.global LINE_1010
.global LINE_1111

.global Int_1
.global Int_2
.global Int_3
.global Int_4
.global Int_5
.global Int_6
.global Int_7
.global _WaitKeyboard
.global CheckBatt

.global _syscall
.global _trap
.global setup_env

|.include "procglo.inc"
.include "signal.inc"

.section _st1,"rx"
.even

/*
 * Bus or Address error exception stack frame:
 *        15     5    4     3    2            0 
 *       +---------+-----+-----+---------------+
 * sp -> |         | R/W | I/N | Function code |
 *       +---------+-----+-----+---------------+
 *       |                 High                |
 *       +- - Access Address  - - - - - - - - -+
 *       |                 Low                 |
 *       +-------------------------------------+
 *       |        Instruction register         |
 *       +-------------------------------------+
 *       |          Status register            |
 *       +-------------------------------------+
 *       |                 High                |
 *       +- - Program Counter - - - - - - - - -+
 *       |                 Low                 |
 *       +-------------------------------------+
 *       R/W (Read/Write): Write = 0, Read = 1.
 *       I/N (Instruction/Not): Instruction = 0, Not = 1.
 */

	.long 0xbeef1001
buserr:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	bsr	bus_error
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

	.long 0xbeef1002
SPURIOUS:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	bsr	spurious
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

	.long 0xbeef1003
ADDRESS_ERROR:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	bsr	address_error
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

	.long 0xbeef1004
ILLEGAL_INSTR:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	bsr	illegal_instr
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

	.long 0xbeef1005
| send signal SIGFPE
ZERO_DIVIDE:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	bsr	zero_divide
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

	.long 0xbeef1006
CHK_INSTR:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	bsr	chk_instr
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

	.long 0xbeef1007
I_TRAPV:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	bsr	i_trapv
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

	.long 0xbeef1008
| send signal SIGILL
PRIVILEGE:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	bsr	privilege
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

	.long 0xbeef1009
TRACE:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	bsr	trace
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

	.long 0xbeef100a
| I don't know (send signal SIGILL?)
| Are there valid 68010+ instructions starting with 1010?
LINE_1010:
	rte

	.long 0xbeef100b
| This should be a floating-point emulator
LINE_1111:
	rte

| Scan for the hardware to know what keys are pressed 
Int_1:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	
	jbsr	scankb

	move	5*4(%sp),-(%sp)		| old ps
	bsr	hardclock
	addq.l	#2,%sp
	
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

/*
 * Does nothing. Why ? Look:
 *  Triggered when the *first* unmasked key (see 0x600019) is *pressed*.
 *  Keeping the key pressed, or pressing another without releasing the first
 *  key, will not generate additional interrupts.  The keyboard is not
 *  debounced in hardware and the interrupt can occasionally be triggered many
 *  times when the key is pressed and sometimes even when the key is released!
 *  So, you understand why you don't use it ;)
 *  Write any value to 0x60001B to acknowledge this interrupt.
 */
Int_2:	move.w	#0x2600,%sr
	move.w	#0x00FF,0x60001A	| acknowledge Int2
oldInt_3:	rte				| Clock for int 3 ?

G = 0x5c00

Int_3:
.if 1
	addq.l	#1,G+0	| realtime.tv_sec++
	clr.l	G+4	| realtime.tv_nsec = 0
.else
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	jbsr	updrealtime
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
.endif
	rte

| Link Auto-Int
Int_4:
	movem.l	%d0-%d2/%a0-%a1,-(%sp)
	bsr	linkintr	| just call the C routine
	movem.l	(%sp)+,%d0-%d2/%a0-%a1
	rte

| System timers.
Int_5:
	/* FIXME: do timers */
	rte

| ON Int.
|	2ND / DIAMOND : Off
|	ESC : Reset
Int_6:
	/* FIXME: handle ON key */
	rte

| send signal SIGSEGV
Int_7:
	/* FIXME */
	jmp	the_beginning

_WaitKeyboard:
	moveq	#0x58,%d0
	dbf	%d0,.
	rts

| Assembly interface for C version; prototype of C version:
| uint32_t syscall(unsigned callno, void **usp, short *sr)
_syscall:
	movem.l	%d3-%d7/%a2-%a6,-(%sp)	| XXX: this is needed for vfork!
	
	lea.l	10*4(%sp),%a0
	move.l	%a0,-(%sp)	| short *sr
	
	move.l	%usp,%a0
	move.l	%a0,-(%sp)	| void **usp
	
	move	%d0,-(%sp)	| int callno
	
	bsr	syscall
	adda.l	#10,%sp
	
	movem.l	(%sp)+,%d3-%d7/%a2-%a6
	rte

| jmp_buf
.equ jmp_buf.reg,0
.equ jmp_buf.sp,10*4
.equ jmp_buf.usp,11*4
.equ jmp_buf.retaddr,12*4

| struct trapframe
.equ trapframe.sr,0
.equ trapframe.pc,2

/* void setup_env(jmp_buf env, struct trapframe *tfp, long *sp);
 * Setup the execution environment "env" using the trapframe "tfp", the stack
 * pointer "sp", and the current user stack pointer. The trapframe is pushed
 * onto the stack (*sp), and the new stack pointer is saved in the environment.
 * FIXME: make this routine cleaner and more elegant!
 */
setup_env:
	move.l	12(%sp),%a0		| %a0 = sp
	move.l	8(%sp),%a1		| %a1 = tfp
	
	| setup the new trap frame
	move.l	trapframe.pc(%a1),-(%a0)	| push pc
	clr	-(%a0)				| clear sr
	
	move.l	4(%sp),%a1		| %a1 = env
	move.l	%a0,jmp_buf.sp(%a1)	| sp
	
	move.l	%usp,%a0
	move.l	%a0,jmp_buf.usp(%a1)	| usp
	
	move.l	1f(%pc),jmp_buf.retaddr(%a1)
	
	move.l	8(%sp),%a0		| %a0 = tfp
	move	#10-1,%d0
0:	move.l	-(%a0),(%a1)+		| copy the saved regs to the child's env
	dbra	%d0,0b
	
	rts

/* the child process resumes here  */
1:
	move	#0,%d0	| return 0 to the child process
	rte

| unused for now
_trap:
	rte
