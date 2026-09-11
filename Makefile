CFLAGS=-Wall -Wno-strict-aliasing -std=gnu11 -g -I. -O0
TARGET ?= x86-64
ifeq ($(TARGET),x86-64)
BACKEND_OBJ=gen.o
TARGET_CFLAGS=-DDEFAULT_TARGET_X86_64
else ifeq ($(TARGET),ex-isa)
BACKEND_OBJ=gen_ex_isa_rebuild.o
TARGET_CFLAGS=-DDEFAULT_TARGET_EX_ISA
else
$(error Unsupported TARGET '$(TARGET)' (use x86-64 or ex-isa))
endif

MAIN_OBJ=main-$(TARGET).o
OBJS=cpp.o debug.o dict.o $(BACKEND_OBJ) lex.o vector.o parse.o buffer.o map.o \
	error.o path.o file.o set.o encoding.o gen_dispatch.o
TESTS := $(patsubst %.c,%.bin,$(filter-out test/testmain.c,$(wildcard test/*.c)))
ECC=./8cc
override CFLAGS += -DBUILD_DIR='"."' $(TARGET_CFLAGS)
ifeq ($(OS),Windows_NT)
RM=cmd /C del /Q
else
RM=rm -f
endif

8cc: 8cc.h $(MAIN_OBJ) $(OBJS)
	cc -o $@ $(MAIN_OBJ) $(OBJS) $(LDFLAGS)

$(OBJS) utiltest.o: 8cc.h keyword.inc

$(MAIN_OBJ): main.c 8cc.h keyword.inc
	$(CC) $(CFLAGS) -c -o $@ $<

utiltest: 8cc.h utiltest.o $(OBJS)
	cc -o $@ utiltest.o $(OBJS) $(LDFLAGS)

test/%.o: test/%.c $(ECC)
	$(ECC) -w -o $@ -c $<

test/%.bin: test/%.o test/testmain.o
	cc -o $@ $< test/testmain.o $(LDFLAGS)

self: 8cc cleanobj
	$(MAKE) CC=$(ECC) CFLAGS= 8cc

test: 8cc $(TESTS)
	$(MAKE) CC=$(ECC) CFLAGS= utiltest
	./utiltest
	./test/ast.sh
	./test/negative.py
	$(MAKE) runtests

runtests:
	@for test in $(TESTS); do  \
	    ./$$test || exit;      \
	done

stage1:
	$(MAKE) cleanobj
	[ -f 8cc ] || $(MAKE) 8cc
	mv 8cc stage1

stage2: stage1
	$(MAKE) cleanobj
	$(MAKE) CC=./stage1 ECC=./stage1 CFLAGS= 8cc
	mv 8cc stage2

stage3: stage2
	$(MAKE) cleanobj
	$(MAKE) CC=./stage2 ECC=./stage2 CFLAGS= 8cc
	mv 8cc stage3

# Compile and run the tests with the default compiler.
testtest:
	$(MAKE) clean
	$(MAKE) $(TESTS)
	$(MAKE) runtests

fulltest: testtest
	$(MAKE) stage1
	$(MAKE) CC=./stage1 ECC=./stage1 CFLAGS= test
	$(MAKE) stage2
	$(MAKE) CC=./stage2 ECC=./stage2 CFLAGS= test
	$(MAKE) stage3
	cmp stage2 stage3

clean: cleanobj
	-$(RM) 8cc 8cc.exe stage?

cleanobj:
	-$(RM) *.o *.s test\\*.o test\\*.bin utiltest utiltest.exe

all: 8cc

.PHONY: clean cleanobj test runtests fulltest self all
