IMAGE = maxxing/compiler-dev
BUILD_DIR = build

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
	$(BUILD_DIR)/src/compiler -ast ./tests/hello.c -o ./debug/hello.ast
run-koopa:
	$(BUILD_DIR)/src/compiler -koopa ./tests/hello.c -o ./debug/hello.koopa
run-riscv:
	$(BUILD_DIR)/src/compiler -riscv ./tests/hello.c -o ./debug/hello.asm

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