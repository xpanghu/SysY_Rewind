#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
COMPILER="${BUILD_DIR}/compiler"
OUT_DIR="${ROOT_DIR}/tmp/regression-smoke"

mkdir -p "${OUT_DIR}"

cmake --build "${BUILD_DIR}" -j12 -- -s

"${COMPILER}" -koopa "${ROOT_DIR}/tests/hello.sysy" \
  -o "${OUT_DIR}/hello.koopa"
"${COMPILER}" -riscv "${ROOT_DIR}/tests/hello.sysy" \
  -o "${OUT_DIR}/hello.s"
"${COMPILER}" -riscv "${ROOT_DIR}/tests/awesome-sysy/lisp.c" \
  -o "${OUT_DIR}/lisp.s"

echo "regression smoke outputs:"
echo "  ${OUT_DIR}/hello.koopa"
echo "  ${OUT_DIR}/hello.s"
echo "  ${OUT_DIR}/lisp.s"

if [[ "${RUN_DOCKER_AUTOTEST:-0}" == "1" ]]; then
  docker run -i --rm \
    -v "${ROOT_DIR}:/root/compiler" \
    maxxing/compiler-dev \
    autotest -riscv -s lv9 /root/compiler
fi
