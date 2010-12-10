
#if __x86_64__
	.text
	.globl	_get_foo
_get_foo:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	_foo@TLVP(%rip), %rdi
	call	*(%rdi)
	popq	%rbp
	ret
#endif


#if __i386__
	.text
	.globl	_get_foo
_get_foo:
	pushl	%ebp
	movl	%esp, %ebp
	subl	$8, %esp
	movl	_foo@TLVP, %eax
	call	*(%eax)    
	movl	%ebp, %esp
	popl	%ebp
	ret
#endif

.subsections_via_symbols
