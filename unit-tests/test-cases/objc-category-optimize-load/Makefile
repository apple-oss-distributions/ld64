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
# Verify optimization where categories are merged into classes
#
OPTIONS = 

ifeq ($(ARCH),i386)
	OPTIONS = -fobjc-abi-version=2 -Wl,-objc_abi_version,2 -isysroot /Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator5.0.sdk -fobjc-legacy-dispatch
endif

all:	all-${ARCH}

all-ppc:
	${PASS_IFF} true

all-i386: all-rest
all-x86_64: all-rest
all-armv6: all-rest
all-armv7: all-rest

all-rest:
	# check optimzation of category methods
	${CC} ${CCFLAGS} ${OPTIONS} -dynamiclib foo.m cat1.m  -framework Foundation -o libfoo.dylib
	size -l libfoo.dylib | grep "__objc_catlist:" | ${FAIL_IF_EMPTY}
	${PASS_IFF_GOOD_MACHO} libfoo.dylib
	
	
clean:
	rm -rf libfoo.dylib
