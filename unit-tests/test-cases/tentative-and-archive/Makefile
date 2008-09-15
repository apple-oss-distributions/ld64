##
# Copyright (c) 2008 Apple Inc. All rights reserved.
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

SHELL = bash # use bash shell so we can redirect just stderr


#
# Test how tentative definitions interact with archives
# main.c has a tenative definition for _var which 
# should *not* cause libfoo.a(foo.o) to be loaded.
#
# <rdar://problem/5779681> ld crashes building XsanFS
# <rdar://problem/5852023> -undefined dynamic_lookup causes spurious extra symbols
#

run: all

all:
	${CC} ${CCFLAGS} foo.c -c -o foo.o
	libtool -static foo.o -o libfoo.a
	${CC} ${CCFLAGS} main.c libfoo.a -o main
	${CC} ${CCFLAGS} main.c libfoo.a -o main -undefined dynamic_lookup
	nm -m main | grep "looked up" | ${FAIL_IF_STDIN}
	${PASS_IFF_GOOD_MACHO} main
		

clean:
	rm -rf main libfoo.a foo.o
