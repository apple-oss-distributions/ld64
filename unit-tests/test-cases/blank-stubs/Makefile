##
# Copyright (c) 2006 Apple Computer, Inc. All rights reserved.
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


ALL_ARCH_OPTIONS = $(patsubst %,-arch %,$(VALID_ARCHS))

#
# Test that blank stubs are handled properly
#

run: all

all:
# build example fully fat dylib
	${CC} `echo ${ALL_ARCH_OPTIONS}` -dynamiclib foo.c -o libfoo.dylib -install_name libfoo.dylib
	lipo libfoo.dylib -remove ${ARCH} -output libfoo.dylib
	lipo -create libfoo.dylib -arch_blank ${ARCH} -output libfoo.dylib
	${CC} ${CCFLAGS} main.c libfoo.dylib -o main
	${OTOOL} -L main | grep libfoo | ${FAIL_IF_STDIN}
	${PASS_IFF_GOOD_MACHO} main
	

clean:
	rm -rf libfoo.dylib main
	