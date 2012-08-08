
// _foo is an exported thread local variable
// _bar is an exported regular variable

		# _a is zerofill global TLV
		.tbss _a$tlv$init,4,2

#if __x86_64__
		.tlv
		.globl _foo
_foo:	.quad	__tlv_bootstrap
		.quad	0
		.quad	_a$tlv$init


#endif

#if __i386__
		.tlv
		.globl _foo
_foo:	.long	__tlv_bootstrap
		.long	0
		.long	_a$tlv$init
#endif


		.data
		.globl _bar
_bar:	.long	0



		.subsections_via_symbols
