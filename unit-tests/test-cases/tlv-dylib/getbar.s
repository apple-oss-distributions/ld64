
#if __x86_64__
	.text
	.globl	_get_bar
_get_bar:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	_bar@TLVP(%rip), %rdi
	call	*(%rdi)
	popq	%rbp
	ret
#endif


#if __i386__
	.text
	.globl	_get_bar
_get_bar:
	pushl	%ebp
	movl	%esp, %ebp
	subl	$8, %esp
	movl	_bar@TLVP, %eax
	call	*(%eax)    
	movl	%ebp, %esp
	popl	%ebp
	ret
#endif

.subsections_via_symbols
