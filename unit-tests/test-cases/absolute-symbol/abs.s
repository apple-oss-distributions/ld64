
  .globl _myAbs
#if __LP64__
	_myAbs = 0x012345678
#else
	_myAbs = 0xfe000000
#endif
