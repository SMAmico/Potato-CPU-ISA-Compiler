CFLAGS=-Wall -Wno-strict-aliasing -std=gnu11 -g -I. -O0
BUILD_DIR=build
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

RAW_OBJS=cpp.o debug.o dict.o $(BACKEND_OBJ) lex.o vector.o parse.o buffer.o map.o \
	error.o path.o file.o set.o encoding.o gen_dispatch.o
MAIN_OBJ=$(BUILD_DIR)/main-$(TARGET).o
OBJS=$(addprefix $(BUILD_DIR)/,$(RAW_OBJS))
UTILTEST_OBJ=$(BUILD_DIR)/utiltest.o
TESTS := $(patsubst %.c,%.bin,$(filter-out test/testmain.c test/ex_isa_link_%.c,$(wildcard test/*.c)))
ECC=./$(BUILD_DIR)/8cc
override CFLAGS += -DBUILD_DIR='"."' $(TARGET_CFLAGS)
ifeq ($(OS),Windows_NT)
RM=cmd /C del /Q
MKDIR=if not exist "$@" mkdir "$@"
else
RM=rm -f
MKDIR=mkdir -p $@
endif

8cc: $(BUILD_DIR)/8cc

$(BUILD_DIR)/8cc: 8cc.h $(MAIN_OBJ) $(OBJS) | $(BUILD_DIR)
	cc -o $@ $(MAIN_OBJ) $(OBJS) $(LDFLAGS)

$(OBJS) $(UTILTEST_OBJ): 8cc.h keyword.inc | $(BUILD_DIR)

$(MAIN_OBJ): main.c 8cc.h keyword.inc
	$(CC) $(CFLAGS) -c -o $@ $<

$(MAIN_OBJ): | $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

utiltest: $(BUILD_DIR)/utiltest

$(BUILD_DIR)/utiltest: 8cc.h $(UTILTEST_OBJ) $(OBJS) | $(BUILD_DIR)
	cc -o $@ $(UTILTEST_OBJ) $(OBJS) $(LDFLAGS)

$(BUILD_DIR):
	$(MKDIR)

test/%.o: test/%.c $(ECC)
	$(ECC) -w -o $@ -c $<

test/%.bin: test/%.o test/testmain.o
	cc -o $@ $< test/testmain.o $(LDFLAGS)

self: 8cc cleanobj
	$(MAKE) CC=$(ECC) CFLAGS= 8cc

test: 8cc $(TESTS)
	$(MAKE) CC=$(ECC) CFLAGS= utiltest
	./$(BUILD_DIR)/utiltest
	./test/ast.sh
	./test/negative.py
	$(MAKE) runtests

runtests:
	@for test in $(TESTS); do  \
	    ./$$test || exit;      \
	done

stage1:
	$(MAKE) cleanobj
	[ -f $(BUILD_DIR)/8cc ] || $(MAKE) 8cc
	mv $(BUILD_DIR)/8cc $(BUILD_DIR)/stage1

stage2: stage1
	$(MAKE) cleanobj
	$(MAKE) CC=./$(BUILD_DIR)/stage1 ECC=./$(BUILD_DIR)/stage1 CFLAGS= 8cc
	mv $(BUILD_DIR)/8cc $(BUILD_DIR)/stage2

stage3: stage2
	$(MAKE) cleanobj
	$(MAKE) CC=./$(BUILD_DIR)/stage2 ECC=./$(BUILD_DIR)/stage2 CFLAGS= 8cc
	mv $(BUILD_DIR)/8cc $(BUILD_DIR)/stage3

# Compile and run the tests with the default compiler.
testtest:
	$(MAKE) clean
	$(MAKE) $(TESTS)
	$(MAKE) runtests

ex-isa-link-test:
	./compile-ex-isa.sh --asm-out test/ex_isa_link.s \
		-o test/ex_isa_link.txt \
		test/ex_isa_link_main.c test/ex_isa_link_provider.c
	./compile-ex-isa.sh --asm-out test/ex_isa_link_static.s \
		-o test/ex_isa_link_static.txt \
		test/ex_isa_link_static_a.c test/ex_isa_link_static_b.c

fulltest: testtest
	$(MAKE) stage1
	$(MAKE) CC=./$(BUILD_DIR)/stage1 ECC=./$(BUILD_DIR)/stage1 CFLAGS= test
	$(MAKE) stage2
	$(MAKE) CC=./$(BUILD_DIR)/stage2 ECC=./$(BUILD_DIR)/stage2 CFLAGS= test
	$(MAKE) stage3
	cmp $(BUILD_DIR)/stage2 $(BUILD_DIR)/stage3

clean: cleanobj
	-$(RM) $(BUILD_DIR)/8cc $(BUILD_DIR)/8cc.exe $(BUILD_DIR)/stage?

cleanobj:
	-$(RM) $(BUILD_DIR)\\*.o *.s test\\*.o test\\*.bin test\\ex_isa_link.s test\\ex_isa_link.txt test\\ex_isa_link.data.txt test\\ex_isa_link_static.s test\\ex_isa_link_static.txt test\\ex_isa_link_static.data.txt $(BUILD_DIR)\\utiltest $(BUILD_DIR)\\utiltest.exe

all: 8cc

.PHONY: clean cleanobj test runtests fulltest self all
