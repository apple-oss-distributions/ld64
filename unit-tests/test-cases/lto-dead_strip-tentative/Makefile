##
# Copyright (c) 2010 Apple Inc. All rights reserved.
#
# @APPLE_LICENSE_HEADER_START@
# 
# This file contains Original Code and/or Modifications of Original Code
# as defined in and that are subject to the Apple Public Source License
# Version 2.0 (the 'License'). You may not use this file except in
# compliance with the License. Please obtain a copy of the License at
# http://www.opensource.apple.com/apsl/ and read it before using this
# file.
# 
# The Original Code and all software distributed under the License are
# distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
# EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
# INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
# Please see the License for the specific language governing rights and
# limitations under the License.
# 
# @APPLE_LICENSE_HEADER_END@
##
TESTROOT = ../..
include ${TESTROOT}/include/common.makefile

#
# <rdar://problem/8708091> Link Time Optimization error with tentative defs and -dead_strip
#

LLVMGCC = /Developer/usr/bin/llvm-gcc-4.2 -arch ${ARCH}
LLVMGXX = /Developer/usr/bin/llvm-g++-4.2 -arch ${ARCH}

run: all

all:
	${LLVMGCC} ${CCFLAGS} --emit-llvm foo.c -c -o foo.o 
	${LLVMGCC} ${CCFLAGS} --emit-llvm bar.c -c -o bar.o 
	${LLVMGCC} ${CCFLAGS} --emit-llvm baz.c -c -o baz.o 
	${LLVMGCC} ${CCFLAGS} main.c -c -o main.o 
	${LLVMGCC} ${CCFLAGS} main.o foo.o bar.o baz.o -o main -dead_strip
	${PASS_IFF_GOOD_MACHO} main
	
clean:
	rm -rf foo.o bar.o baz.o main.o main
