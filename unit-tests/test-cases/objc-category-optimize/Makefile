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
	OPTIONS = -fobjc-abi-version=2 -Wl,-objc_abi_version,2
endif

all:	all-${ARCH}

all-ppc:
	${PASS_IFF} true

all-i386: all-rest
all-x86_64: all-rest
all-armv6: all-rest

all-rest:
	# check optimization can be turned off
	${CC} ${CCFLAGS} ${OPTIONS} -dynamiclib foo.m cat1.m cat2.m -framework Foundation -Wl,-no_objc_category_merging -o libno.dylib
	size -l libno.dylib | grep "__objc_catlist: 0" | ${FAIL_IF_STDIN}
	otool -ov libno.dylib | grep -A17 __objc_classlist | grep -A16 '_OBJC_CLASS_$$_Foo' | grep "count 1" | ${FAIL_IF_EMPTY}
	# check optimzation of category methods
	${CC} ${CCFLAGS} ${OPTIONS} -dynamiclib foo.m cat1.m cat2.m -framework Foundation -o libfoo.dylib
	size -l libfoo.dylib | grep "__objc_catlist: 0" | ${FAIL_IF_EMPTY}
	otool -ov libfoo.dylib | grep -A20 __objc_classlist | grep -A20 '_OBJC_CLASS_$$_Foo' | grep "count 4" | ${FAIL_IF_EMPTY}
	# check optimzation of protocol and category methods
	${CC} ${CCFLAGS} ${OPTIONS} -dynamiclib foo.m cat1.m cat2.m -DPROTOCOLS -framework Foundation -o libfoo2.dylib
	size -l libfoo2.dylib | grep "__objc_catlist: 0" | ${FAIL_IF_EMPTY}
	otool -ov libfoo2.dylib | grep -A20 __objc_classlist | grep -A20 '_OBJC_CLASS_$$_Foo' | grep "count 6" | ${FAIL_IF_EMPTY}
	# check optimzation of properties and category methods
	${CC} ${CCFLAGS} ${OPTIONS} -dynamiclib foo.m cat1.m cat2.m -DPROPERTIES -framework Foundation -o libfoo3.dylib
	size -l libfoo3.dylib | grep "__objc_catlist: 0" | ${FAIL_IF_EMPTY}
	otool -ov libfoo3.dylib | grep -A20 __objc_classlist | grep -A20 '_OBJC_CLASS_$$_Foo' | grep "count 6" | ${FAIL_IF_EMPTY}
	# check optimzation of category methods and no base methods
	${CC} ${CCFLAGS} ${OPTIONS} -dynamiclib foo.m cat1.m cat2.m -DNO_BASE_METHODS -framework Foundation -o libfoo4.dylib
	size -l libfoo4.dylib | grep "__objc_catlist: 0" | ${FAIL_IF_EMPTY}
	otool -ov libfoo4.dylib | grep -A20 __objc_classlist | grep -A20 '_OBJC_CLASS_$$_Foo' | grep "count 3" | ${FAIL_IF_EMPTY}
	otool -ov libfoo4.dylib | grep -A20 "Meta Class" | grep "count 2" | ${FAIL_IF_EMPTY}
	${PASS_IFF_GOOD_MACHO} libfoo3.dylib
	
	
	
clean:
	rm -rf  lib*.dylib
