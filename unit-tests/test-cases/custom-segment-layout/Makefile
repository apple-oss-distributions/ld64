
TESTROOT = ../..
include ${TESTROOT}/include/common.makefile

#
# Check that a large page zero works
#

run: all

all:
	${CC} ${CCFLAGS} main.c zero.s -o main -pagezero_size 0 -segaddr __TEXT 0xb8000000 -segaddr __MYZEROPAGE 0
	${PASS_IFF_GOOD_MACHO} main

clean:
	rm -f main
