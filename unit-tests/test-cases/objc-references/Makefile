##
# Copyright (c) 2005 Apple Computer, Inc. All rights reserved.
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
# The point of this test is to verify an Objective-C object file
# is parsed to find the proper class references
#

run: all

all:
	${CC} ${CCFLAGS} test.m -c -o test.${ARCH}.o
	${LD} -arch ${ARCH} -r test.${ARCH}.o -o test-r.${ARCH}.o
	nm test-r.${ARCH}.o | grep '.objc_class_name_NSObject' | ${FAIL_IF_EMPTY} 
	nm test-r.${ARCH}.o | grep '.objc_class_name_NSData'   | ${FAIL_IF_EMPTY} 
	nm test-r.${ARCH}.o | grep '.objc_class_name_NSArray'  | ${FAIL_IF_EMPTY} 
	nm test-r.${ARCH}.o | grep '.objc_class_name_NSString' | ${PASS_IFF_STDIN} 

clean:
	rm -rf test.${ARCH}.o test.${ARCH}.o.dump
		

	#${OBJECTDUMP} -no_content test.${ARCH}.o > test.${ARCH}.o.dump
	#grep '.objc_class_name_NSObject' test.${ARCH}.o.dump | ${FAIL_IF_EMPTY} 
	#grep '.objc_class_name_NSString' test.${ARCH}.o.dump | ${PASS_IFF_STDIN} 
