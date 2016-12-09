##
# Copyright (c) 2007-2008 Apple Inc. All rights reserved.
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
# Validate that the linker catches illegal combinations of .o files 
# compiled with different GC settings. 
#

IMAGE_INFO = "__objc_imageinfo"
ifeq ($(ARCH),i386)
	IMAGE_INFO = "__image_info"
endif

ifeq ($(ARCH),i386)
	ALL = all-gc
else
ifeq ($(ARCH),x86_64)
	ALL = all-gc
else
	ALL = all-nogc
endif
endif

all: ${ALL}

# For platforms that do not support GC.
all-nogc:
	${PASS_IFF} true

# For platforms that support GC.
all-gc:
	${CC} ${CCFLAGS} foo.m -c -o foo.o
	${FAIL_IF_BAD_OBJ} foo.o

	${CC} ${CCFLAGS} bar.m -c -o bar.o
	${FAIL_IF_BAD_OBJ} bar.o

	# clang no longer builds GC so we create fake GC object files instead.

	cp -f foo.o foo-gc.o
	${OBJCIMAGEINFO} +supports-gc foo-gc.o

	cp -f foo.o foo-gc-only.o
	${OBJCIMAGEINFO} +supports-gc +requires-gc foo-gc-only.o

	cp -f bar.o bar-gc.o
	${OBJCIMAGEINFO} +supports-gc bar-gc.o

	cp -f bar.o bar-gc-only.o
	${OBJCIMAGEINFO} +supports-gc +requires-gc bar-gc-only.o

	# check RR + RR -> RR
	${CC} ${CCFLAGS} foo.o bar.o runtime.c -dynamiclib -o libfoobar.dylib -framework Foundation
	${FAIL_IF_BAD_MACHO} libfoobar.dylib

	# check GC/RR + GC/RR -> GC/RR
	${CC} ${CCFLAGS} foo-gc.o bar-gc.o runtime.c -dynamiclib -o libfoobar.dylib  -framework Foundation
	${FAIL_IF_BAD_MACHO} libfoobar.dylib
	${OBJCIMAGEINFO} libfoobar.dylib | grep supports-gc | ${FAIL_IF_EMPTY}

	# check GC + GC -> GC
	${CC} ${CCFLAGS} foo-gc-only.o bar-gc-only.o runtime.c -dynamiclib -o libfoobar.dylib  -framework Foundation
	${FAIL_IF_BAD_MACHO} libfoobar.dylib
	${OBJCIMAGEINFO} libfoobar.dylib | grep 'supports-gc requires-gc' | ${FAIL_IF_EMPTY}

	# check RR + GC/RR -> RR
	${CC} ${CCFLAGS} foo.o bar-gc.o runtime.c -dynamiclib -o libfoobar.dylib  -framework Foundation
	${FAIL_IF_BAD_MACHO} libfoobar.dylib
	${OBJCIMAGEINFO} libfoobar.dylib | grep gc | ${FAIL_IF_STDIN}

	# check GC/RR + RR -> RR
	${CC} ${CCFLAGS} bar-gc.o foo.o runtime.c -dynamiclib -o libfoobar.dylib  -framework Foundation
	${FAIL_IF_BAD_MACHO} libfoobar.dylib
	${OBJCIMAGEINFO} libfoobar.dylib | grep gc | ${FAIL_IF_STDIN}

	# check GC + GC/RR -> GC
	${CC} ${CCFLAGS} foo-gc-only.o bar-gc.o runtime.c -dynamiclib -o libfoobar.dylib   -framework Foundation
	${FAIL_IF_BAD_MACHO} libfoobar.dylib
	${OBJCIMAGEINFO} libfoobar.dylib | grep 'supports-gc requires-gc' | ${FAIL_IF_EMPTY}

	# check RR + GC -> error
	${FAIL_IF_SUCCESS} ${CC} ${CCFLAGS} foo.o bar-gc-only.o runtime.c -dynamiclib -o libfoobar.dylib  -framework Foundation 2> fail.log

	# check cmd line GC/RR, GC/RR + RR -> error
	${FAIL_IF_SUCCESS} ${CC} ${CCFLAGS} foo-gc.o foo.o runtime.c -dynamiclib -o libfoobar.dylib -Wl,-objc_gc  -framework Foundation 2> fail.log

	# check GC/RR + compaction
	${CC} ${CCFLAGS} foo-gc.o bar-gc.o runtime.c -dynamiclib -Wl,-objc_gc_compaction -o libfoobar.dylib  -framework Foundation
	${OBJCIMAGEINFO} libfoobar.dylib | grep 0x12 | ${FAIL_IF_EMPTY}

	# check GC + compaction
	${CC} ${CCFLAGS} foo-gc-only.o bar-gc-only.o runtime.c -dynamiclib -Wl,-objc_gc_compaction -o libfoobar.dylib  -framework Foundation
	${OBJCIMAGEINFO} libfoobar.dylib | grep 0x16 | ${FAIL_IF_EMPTY}

	# none + GC/RR-dylib -> none
	${CC} ${CCFLAGS} foo-gc.o runtime.c -dynamiclib -o libfoo.dylib  -framework Foundation
	${CC} ${CCFLAGS} none.c libfoo.dylib -dynamiclib -o libnone.dylib -framework Foundation
	size -m -l libnone.dylib | grep ${IMAGE_INFO} | ${FAIL_IF_STDIN}

	# none + GC-dylib -> none
	${CC} ${CCFLAGS} foo-gc-only.o runtime.c -dynamiclib -o libfoo.dylib  -framework Foundation
	${CC} ${CCFLAGS} none.c libfoo.dylib -dynamiclib -o libnone.dylib -framework Foundation
	size -m -l libnone.dylib | grep ${IMAGE_INFO} | ${FAIL_IF_STDIN}

	# none + RR-dylib -> none
	${CC} ${CCFLAGS} foo.o runtime.c -dynamiclib -o libfoo.dylib  -framework Foundation
	${CC} ${CCFLAGS} none.c libfoo.dylib -dynamiclib -o libnone.dylib -framework Foundation
	size -m -l libnone.dylib | grep ${IMAGE_INFO} | ${FAIL_IF_STDIN}

	# check RR + GC-dylib -> error
	${CC} ${CCFLAGS} foo-gc-only.o runtime.c -dynamiclib -o libfoo.dylib  -framework Foundation
	${FAIL_IF_SUCCESS} ${CC} ${CCFLAGS} bar.o runtime.c -dynamiclib libfoo.dylib -o libbar.dylib  -framework Foundation 2> fail.log

	# check GC + RR-dylib -> error
	${CC} ${CCFLAGS} foo.o runtime.c -dynamiclib -o libfoo.dylib  -framework Foundation
	${FAIL_IF_SUCCESS} ${CC} ${CCFLAGS} bar-gc-only.o runtime.c -dynamiclib libfoo.dylib -o libbar.dylib  -framework Foundation 2> fail.log

	${PASS_IFF} true

clean:
	rm -rf foo*.o bar*.o libfoobar.dylib fail.log libfoo.dylib libnone.dylib
