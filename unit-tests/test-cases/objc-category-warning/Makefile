##
# Copyright (c) 2011 Apple Inc. All rights reserved.
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
# Verify optimization in which categories are merged into classes
#
OPTIONS = 

ifeq ($(ARCH),i386)
	OPTIONS = -fobjc-abi-version=2 -Wl,-objc_abi_version,2
endif

all:	all-${FILEARCH}

all-ppc:
	${PASS_IFF} true

all-i386: all-rest
all-x86_64: all-rest
all-arm: all-rest

all-rest:
	${CC} ${CCFLAGS} ${OPTIONS} -dynamiclib foo.m cat.m -DOVERRIDE_CLASS=1 -framework Foundation -o libfoo.dylib 2> warnings1.txt
	grep warning warnings1.txt | grep instance_method | ${FAIL_IF_EMPTY}
	grep warning warnings1.txt | grep class_method | ${FAIL_IF_EMPTY}
	${CC} ${CCFLAGS} ${OPTIONS} -dynamiclib foo.m cat.m copycat.m -framework Foundation -o libfoo2.dylib 2> warnings2.txt
	grep warning warnings2.txt | grep instance_method_fromcat | ${FAIL_IF_EMPTY}
	grep warning warnings2.txt | grep class_method_fromcat | ${FAIL_IF_EMPTY}
	${PASS_IFF_GOOD_MACHO} libfoo2.dylib
	
clean:
	rm -rf  lib*.dylib warnings*.txt