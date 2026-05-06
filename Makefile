IMAGE = maxxing/compiler-dev
BUILD_DIR = build
COMPILER = $(BUILD_DIR)/compiler
SIMPLE_TEST = ./tests/hello.sysy
INPUT ?= $(SIMPLE_TEST)
INPUT_DATA ?=
RISCV_OUT_DIR = riscv32-baremetal

PROGRAM_NAME = $(basename $(notdir $(INPUT)))
PROGRAM_DIR = $(RISCV_OUT_DIR)/$(PROGRAM_NAME)
RISCV_ASM = $(PROGRAM_DIR)/$(PROGRAM_NAME).s
RISCV_ELF = $(PROGRAM_DIR)/$(PROGRAM_NAME).elf

SYSYRT_DIR = third_party/sysyrt/baremetal
SYSYRT_BUILD_DIR = $(BUILD_DIR)/sysyrt/riscv32-baremetal
SYSYRT_SRC = $(SYSYRT_DIR)/sysy_baremetal.c
SYSYRT_OBJ = $(SYSYRT_BUILD_DIR)/sysy_baremetal.o
SYSYRT_LIB = $(SYSYRT_BUILD_DIR)/libsysy.a
CRT0_SRC = $(SYSYRT_DIR)/crt0.S
CRT0_OBJ = $(SYSYRT_BUILD_DIR)/crt0.o
LINKER_SCRIPT = $(SYSYRT_DIR)/linker.ld

CROSS_CC ?= riscv64-unknown-elf-gcc
CROSS_AR ?= riscv64-unknown-elf-ar
QEMU ?= qemu-system-riscv32

ARCH_FLAGS = -march=rv32im -mabi=ilp32
BAREMETAL_CFLAGS = -O2 -ffreestanding -fno-builtin -nostdlib -Wall
BAREMETAL_LDFLAGS = -nostdlib -Wl,-T,$(LINKER_SCRIPT) -Wl,--gc-sections
QEMU_FLAGS = -M virt -bios none -m 128M -display none -monitor none -serial stdio -kernel

UID := $(shell id -u)
GID := $(shell id -g)
PWD := $(shell pwd)

.PHONY: all config build clean clean-riscv run-ast run-koopa run-riscv sysyrt riscv-asm riscv-elf run-riscv-baremetal

all: build

config:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_CXX_COMPILER=clang++

build: $(COMPILER)

$(COMPILER): config
#cmake --build $(BUILD_DIR) -j12
#可选，不会出现层级日志
	cmake --build $(BUILD_DIR) -j12 -- -s  

clean:
	rm -rf $(BUILD_DIR) $(RISCV_OUT_DIR)

clean-riscv:
	rm -rf $(RISCV_OUT_DIR)

run-ast:
	$(COMPILER) -ast $(SIMPLE_TEST) -o ./debug/hello.ast
run-koopa:
	$(COMPILER) -koopa $(SIMPLE_TEST) -o ./debug/hello.koopa
run-riscv:
	$(COMPILER) -riscv $(SIMPLE_TEST) -o ./debug/hello.asm

$(SYSYRT_BUILD_DIR):
	mkdir -p $@

$(PROGRAM_DIR):
	mkdir -p $@

$(SYSYRT_OBJ): $(SYSYRT_SRC) | $(SYSYRT_BUILD_DIR)
	$(CROSS_CC) $(ARCH_FLAGS) $(BAREMETAL_CFLAGS) -c $< -o $@

$(SYSYRT_LIB): $(SYSYRT_OBJ) | $(SYSYRT_BUILD_DIR)
	$(CROSS_AR) rcs $@ $<

$(CRT0_OBJ): $(CRT0_SRC) | $(SYSYRT_BUILD_DIR)
	$(CROSS_CC) $(ARCH_FLAGS) $(BAREMETAL_CFLAGS) -c $< -o $@

sysyrt: $(SYSYRT_LIB) $(CRT0_OBJ)

$(RISCV_ASM): $(INPUT) $(COMPILER) | $(PROGRAM_DIR)
	$(COMPILER) -riscv $(INPUT) -o $@

riscv-asm: $(RISCV_ASM)
	@echo "RISC-V assembly: $(RISCV_ASM)"

$(RISCV_ELF): $(RISCV_ASM) $(CRT0_OBJ) $(SYSYRT_LIB) $(LINKER_SCRIPT) | $(PROGRAM_DIR)
	$(CROSS_CC) $(ARCH_FLAGS) $(BAREMETAL_LDFLAGS) $(CRT0_OBJ) $(RISCV_ASM) $(SYSYRT_LIB) -o $@

riscv-elf: $(RISCV_ELF)
	@echo "RISC-V assembly: $(RISCV_ASM)"
	@echo "RISC-V ELF: $(RISCV_ELF)"

run-riscv-baremetal: $(RISCV_ELF)
	@echo "QEMU command: $(QEMU) $(QEMU_FLAGS) $(RISCV_ELF)"
ifeq ($(strip $(INPUT_DATA)),)
	$(QEMU) $(QEMU_FLAGS) $(RISCV_ELF)
else
	{ cat $(INPUT_DATA); printf '\004'; } | $(QEMU) $(QEMU_FLAGS) $(RISCV_ELF)
endif
