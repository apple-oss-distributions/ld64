/*
 * Copyright (c) 2005 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#if __ppc__ || __ppc64__

	.text
	.align 2
		
	.globl _test_loads
_test_loads:
	stmw r30,-8(r1)
	stwu r1,-48(r1)
Lpicbase:

	; PIC load of a 
	addis r2,r10,ha16(_a-Lpicbase)
	lwz r2,lo16(_a-Lpicbase)(r2)

	; PIC load of c 
	addis r2,r10,ha16(_c-Lpicbase)
	lwz r2,lo16(_c-Lpicbase)(r2)

	; absolute load of a
	lis r2,ha16(_a)
	lwz r2,lo16(_a)(r2)

	; absolute load of c
	lis r2,ha16(_c)
	lwz r2,lo16(_c)(r2)

	; absolute load of external
	lis r2,ha16(_ax)
	lwz r2,lo16(_ax)(r2)

	; absolute lea of external
	lis r2,hi16(_ax)
	ori r2,r2,lo16(_ax)


	; PIC load of a + addend
	addis r2,r10,ha16(_a+0x19000-Lpicbase)
	lwz r2,lo16(_a+0x19000-Lpicbase)(r2)

	; absolute load of a + addend
	lis r2,ha16(_a+0x19000)
	lwz r2,lo16(_a+0x19000)(r2)

	; absolute load of external + addend
	lis r2,ha16(_ax+0x19000)
	lwz r2,lo16(_ax+0x19000)(r2)

	; absolute lea of external + addend
	lis r2,hi16(_ax+0x19000)
	ori r2,r2,lo16(_ax+0x19000)


	; PIC load of a + addend
	addis r2,r10,ha16(_a+0x09000-Lpicbase)
	lwz r2,lo16(_a+0x09000-Lpicbase)(r2)

	; absolute load of a + addend
	lis r2,ha16(_a+0x09000)
	lwz r2,lo16(_a+0x09000)(r2)

	; absolute load of external + addend
	lis r2,ha16(_ax+0x09000)
	lwz r2,lo16(_ax+0x09000)(r2)

	; absolute lea of external + addend
	lis r2,hi16(_ax+0x09000)
	ori r2,r2,lo16(_ax+0x09000)

	blr


_test_calls:
	; call internal
	bl	_test_branches
	
	; call internal + addend
	bl	_test_branches+0x19000

	; call external
	bl	_external
	
	; call external + addend
	bl	_external+0x19000
	

_test_branches:
	; call internal
	bne	_test_calls
	
	; call internal + addend
	bne	_test_calls+16

	; call external
	bne	_external
	
	; call external + addend
	bne	_external+16
#endif



#if __i386__
	.text
	.align 2
	
	.globl _test_loads
_test_loads:
	pushl	%ebp
Lpicbase:

	# PIC load of a 
	movl	_a-Lpicbase(%ebx), %eax
	
	# absolute load of a
	movl	_a, %eax

	# absolute load of external
	movl	_ax, %eax

	# absolute lea of external
	leal	_ax, %eax


	# PIC load of a + addend
	movl	_a-Lpicbase+0x19000(%ebx), %eax

	# absolute load of a + addend
	movl	_a+0x19000(%ebx), %eax

	# absolute load of external + addend
	movl	_ax+0x19000(%ebx), %eax

	# absolute lea of external + addend
	leal	_ax+0x1900, %eax

	ret


_test_calls:
	# call internal
	call	_test_branches
	
	# call internal + addend
	call	_test_branches+0x19000

	# call external
	call	_external
	
	# call external + addend
	call	_external+0x19000
	

_test_branches:
	# call internal
	jne	_test_calls
	
	# call internal + addend
	jne	_test_calls+16

	# call external
	jne	_external
	
	# call external + addend
	jne	_external+16
#endif



	# test that pointer-diff relocs are preserved
	.text
_test_diffs:
	.align 2
Llocal2:
	.long 0
	.long Llocal2-_test_branches
#if __ppc64__
	.quad Llocal2-_test_branches
#endif


	.data
_a:	
	.long	0

_b:
#if __ppc__ || __i386__
	.long	_test_calls
	.long	_test_calls+16
	.long	_external
	.long	_external+16
#elif __ppc64__
	.quad	_test_calls
	.quad	_test_calls+16
	.quad	_external
	.quad	_external+16
#endif

	# test that reloc sizes are the same
Llocal3:
	.long	0
	
Llocal4:
	.long 0
	
	.long Llocal4-Llocal3
	
Lfiller:
	.space	0x9000
_c:
	.long	0

