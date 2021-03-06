/* copyin and copyout
 * these catch obvious invalid memory ranges.
 * since the MC68000 doesn't have an MMU, we can only do so much, as far
 * as memory protection and fault detection go. :(
 */

.global copyout
.global copyin
.global badbuffer

.section _st1, "x"

| int badbuffer(void *base, size_t size);
| return 0 if all parts of buffer are in bounds
| return non-zero otherwise
badbuffer:
	move.l	4(%sp),%d1	| base
	move.l	8(%sp),%d0	| size

	| chkaddr returns from this routine with a non-zero value in %d0
	| if the address is not valid
	bsr.s	chkaddr		
	moveq.l	#0,%d0		| it's valid
	rts

| check %d1 through %d1 + %d0 for valid address-ness
chkaddr:
	cmp.l	#0x6000,%d1	| low address XXX: constant
	blo	1f
	move.l	%d1,%d2
	subq.l	#1,%d2
	add.l	%d0,%d2		| %d2 is end address of range
	bcs	1f		| check for wrap-around
	cmp.l	#0x40000,%d2	| high address
	bhs	1f
	rts	| looks aight...
1:	| Fault!
	lea.l	4(%sp),%sp	| pop off our return address
	move	#14,%d0		| XXX: EFAULT
	rts

/* int copyin(void *dest, void *src, size_t count) */
copyin:
	move.l	12(%sp),%d0	| count
	beq.b	1f
	move.l	8(%sp),%d1	| src
	bsr	chkaddr
	move.l	%d1,%a1
	move.l	4(%sp),%a0	| dest
0:	move.b	(%a1)+,(%a0)+
	subq.l	#1,%d0
	bne	0b
1:	rts

/* int copyout(void *dest, void *src, size_t count) */
copyout:
	move.l	12(%sp),%d0	| count
	beq.b	1f
	move.l	4(%sp),%d1	| dest
	bsr	chkaddr
	move.l	%d1,%a0
	move.l	8(%sp),%a1	| src
0:	move.b	(%a1)+,(%a0)+
	subq.l	#1,%d0
	bne	0b
1:	rts
