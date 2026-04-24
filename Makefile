IMAGE = maxxing/compiler-dev
BUILD_DIR = build
COMPILER = $(BUILD_DIR)/compiler
SIMPLE_TEST = ./tests/hello.sysy
BAREMETAL_RUNNER = ./scripts/run_riscv_baremetal.sh
INPUT ?= $(SIMPLE_TEST)
INPUT_DATA ?=

UID := $(shell id -u)
GID := $(shell id -g)
PWD := $(shell pwd)

all: build

config:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_CXX_COMPILER=clang++

build: config
#cmake --build $(BUILD_DIR) -j12
#可选，不会出现层级日志
	cmake --build $(BUILD_DIR) -j12 -- -s  

clean:
	rm -rf $(BUILD_DIR)

run-ast:
	$(COMPILER) -ast $(SIMPLE_TEST) -o ./debug/hello.ast
run-koopa:
	$(COMPILER) -koopa $(SIMPLE_TEST) -o ./debug/hello.koopa
run-riscv:
	$(COMPILER) -riscv $(SIMPLE_TEST) -o ./debug/hello.asm
run-riscv-baremetal:
ifeq ($(strip $(INPUT_DATA)),)
	$(BAREMETAL_RUNNER) $(INPUT)
else
	$(BAREMETAL_RUNNER) $(INPUT) $(INPUT_DATA)
endif
