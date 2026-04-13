IMAGE = maxxing/compiler-dev
BUILD_DIR = build
COMPILER = $(BUILD_DIR)/compiler
SIMPLE_TEST = ./tests/hello.sysy

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

shell:
	docker run -it --rm \
			-u $(UID):$(GID) \
			-v "$(PWD):$(PWD)" \
			-w "$(PWD)" \
			$(IMAGE) bash

run-ast:
	$(COMPILER) -ast $(SIMPLE_TEST) -o ./debug/hello.ast
run-koopa:
	$(COMPILER) -koopa $(SIMPLE_TEST) -o ./debug/hello.koopa
run-riscv:
	$(COMPILER) -riscv $(SIMPLE_TEST) -o ./debug/hello.asm

#test:
#python3 scripts/test_runner.py koopa
#python3 scripts/test_runner.py riscv

docker-build:
	docker run --rm \
		-u $(UID):$(GID) \
		-v "$(PWD):$(PWD)" \
		-w "$(PWD)" \
		$(IMAGE) \
		sh -c "cmake -S . -B $(BUILD_DIR) && cmake --build $(BUILD_DIR) -j12"