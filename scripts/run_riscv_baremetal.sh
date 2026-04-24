#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER_BIN="$ROOT_DIR/build/compiler"
RUNTIME_DIR="$ROOT_DIR/third_party/sysyrt/baremetal"
RUNTIME_BUILD_DIR="$ROOT_DIR/third_party/sysyrt/riscv32-baremetal"
RUNTIME_LIB="$RUNTIME_BUILD_DIR/libsysy.a"
CRT0_SRC="$RUNTIME_DIR/crt0.S"
RUNTIME_SRC="$RUNTIME_DIR/sysy_baremetal.c"
LINKER_SCRIPT="$RUNTIME_DIR/linker.ld"

CC="${CC:-riscv64-unknown-elf-gcc}"
AR="${AR:-riscv64-unknown-elf-ar}"
QEMU="${QEMU:-qemu-system-riscv32}"

ARCH_FLAGS=(-march=rv32im -mabi=ilp32)
CFLAGS=(-O2 -ffreestanding -fno-builtin -nostdlib -Wall)
LDFLAGS=(-nostdlib -Wl,-T,"$LINKER_SCRIPT" -Wl,--gc-sections)
QEMU_FLAGS=(-M virt -bios none -m 128M -display none -monitor none -serial stdio -kernel)

usage() {
  cat <<EOF
Usage: $0 [input.sysy] [program-input-file]

Build and run a SysY program locally through the bare-metal RISC-V route:
  SysY -> RISC-V assembly -> ELF -> qemu-system-riscv32

If no input file is given, the script defaults to:
  tests/hello.sysy
EOF
}

resolve_input_path() {
  local raw="$1"
  shift || true

  local candidates=()
  candidates+=("$raw")
  candidates+=("$ROOT_DIR/$raw")
  candidates+=("$ROOT_DIR/tests/$raw")

  if [[ "$raw" != *.sysy ]]; then
    candidates+=("$raw.sysy")
    candidates+=("$ROOT_DIR/$raw.sysy")
    candidates+=("$ROOT_DIR/tests/$raw.sysy")
  fi

  for path in "${candidates[@]}"; do
    if [[ -f "$path" ]]; then
      printf '%s\n' "$path"
      return 0
    fi
  done

  echo "input file not found: $raw" >&2
  echo "searched paths:" >&2
  for path in "${candidates[@]}"; do
    echo "  $path" >&2
  done
  return 1
}

resolve_input_data_path() {
  local raw="$1"
  local candidates=()
  candidates+=("$raw")
  candidates+=("$ROOT_DIR/$raw")
  candidates+=("$ROOT_DIR/tests/$raw")

  for path in "${candidates[@]}"; do
    if [[ -f "$path" ]]; then
      printf '%s\n' "$path"
      return 0
    fi
  done

  echo "input data file not found: $raw" >&2
  echo "searched paths:" >&2
  for path in "${candidates[@]}"; do
    echo "  $path" >&2
  done
  return 1
}

need_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing required tool: $1" >&2
    exit 1
  fi
}

build_runtime() {
  mkdir -p "$RUNTIME_BUILD_DIR"
  local obj="$RUNTIME_BUILD_DIR/sysy_baremetal.o"
  if [[ ! -f "$RUNTIME_LIB" || "$RUNTIME_SRC" -nt "$obj" || "$RUNTIME_LIB" -nt "$obj" ]]; then
    :
  fi
  "$CC" "${ARCH_FLAGS[@]}" "${CFLAGS[@]}" -c "$RUNTIME_SRC" -o "$obj"
  "$AR" rcs "$RUNTIME_LIB" "$obj"
}

build_compiler_if_needed() {
  if [[ ! -x "$COMPILER_BIN" ]]; then
    cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build"
    cmake --build "$ROOT_DIR/build" -j4
  fi
}

if [[ $# -gt 2 ]]; then
  usage
  exit 1
fi

INPUT_SYSY_RAW="${1:-tests/hello.sysy}"
INPUT_DATA_RAW="${2:-}"

need_tool "$CC"
need_tool "$AR"
need_tool "$QEMU"

INPUT_SYSY="$(resolve_input_path "$INPUT_SYSY_RAW")"

if [[ -n "$INPUT_DATA_RAW" ]]; then
  INPUT_DATA="$(resolve_input_data_path "$INPUT_DATA_RAW")"
else
  INPUT_DATA=""
fi

build_compiler_if_needed
build_runtime

WORK_DIR="$(mktemp -d /tmp/sysy-baremetal.XXXXXX)"
trap 'rm -rf "$WORK_DIR"' EXIT

ASM_FILE="$WORK_DIR/program.s"
ELF_FILE="$WORK_DIR/program.elf"
CRT0_OBJ="$WORK_DIR/crt0.o"

"$COMPILER_BIN" -riscv "$INPUT_SYSY" -o "$ASM_FILE"
"$CC" "${ARCH_FLAGS[@]}" "${CFLAGS[@]}" -c "$CRT0_SRC" -o "$CRT0_OBJ"
"$CC" "${ARCH_FLAGS[@]}" "${LDFLAGS[@]}" \
  "$CRT0_OBJ" "$ASM_FILE" "$RUNTIME_LIB" -o "$ELF_FILE"

if [[ -n "$INPUT_DATA" ]]; then
  exec "$QEMU" "${QEMU_FLAGS[@]}" "$ELF_FILE" < "$INPUT_DATA"
else
  exec "$QEMU" "${QEMU_FLAGS[@]}" "$ELF_FILE"
fi
