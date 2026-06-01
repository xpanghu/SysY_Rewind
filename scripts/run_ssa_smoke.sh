#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
COMPILER="${BUILD_DIR}/compiler"
OUT_DIR="${ROOT_DIR}/tmp/ssa-smoke"

mkdir -p "${OUT_DIR}"

cmake --build "${BUILD_DIR}" -j12 -- -s

cat > "${OUT_DIR}/if_else.sysy" <<'SYSY'
int main() {
  int x;
  if (1) {
    x = 1;
  } else {
    x = 2;
  }
  return x;
}
SYSY

cat > "${OUT_DIR}/while_loop.sysy" <<'SYSY'
int main() {
  int i = 0;
  while (i < 3) {
    i = i + 1;
  }
  return i;
}
SYSY

"${COMPILER}" -ssa "${OUT_DIR}/if_else.sysy" -o "${OUT_DIR}/if_else.ssa.koopa"
"${COMPILER}" -ssa "${OUT_DIR}/while_loop.sysy" -o "${OUT_DIR}/while_loop.ssa.koopa"

assert_contains() {
  local file="$1"
  local pattern="$2"
  if ! grep -Eq "$pattern" "$file"; then
    echo "expected pattern not found in ${file}: ${pattern}" >&2
    cat "$file" >&2
    exit 1
  fi
}

assert_not_contains() {
  local file="$1"
  local pattern="$2"
  if grep -Eq "$pattern" "$file"; then
    echo "unexpected pattern found in ${file}: ${pattern}" >&2
    cat "$file" >&2
    exit 1
  fi
}

# The key validation is source -> AST -> memory-form IR -> Mem2RegPass -> SSA IR,
# not hand-constructed SSA IR.
assert_contains "${OUT_DIR}/if_else.ssa.koopa" '^%.*\(%x_1_phi_0: i32\):$'
assert_contains "${OUT_DIR}/if_else.ssa.koopa" 'jump %.*\(1\)'
assert_contains "${OUT_DIR}/if_else.ssa.koopa" 'jump %.*\(2\)'
assert_contains "${OUT_DIR}/if_else.ssa.koopa" 'ret %x_1_phi_0'
assert_not_contains "${OUT_DIR}/if_else.ssa.koopa" 'mem2reg'
assert_not_contains "${OUT_DIR}/if_else.ssa.koopa" 'alloc i32'
assert_not_contains "${OUT_DIR}/if_else.ssa.koopa" 'load '
assert_not_contains "${OUT_DIR}/if_else.ssa.koopa" 'store '

assert_contains "${OUT_DIR}/while_loop.ssa.koopa" '^%.*\(%i_1_phi_0: i32\):$'
assert_contains "${OUT_DIR}/while_loop.ssa.koopa" 'jump %.*\(0\)'
assert_contains "${OUT_DIR}/while_loop.ssa.koopa" 'jump %.*\(%'
assert_contains "${OUT_DIR}/while_loop.ssa.koopa" 'ret %i_1_phi_0'
assert_not_contains "${OUT_DIR}/while_loop.ssa.koopa" 'mem2reg'
assert_not_contains "${OUT_DIR}/while_loop.ssa.koopa" 'alloc i32'
assert_not_contains "${OUT_DIR}/while_loop.ssa.koopa" 'load '
assert_not_contains "${OUT_DIR}/while_loop.ssa.koopa" 'store '

echo "SSA smoke outputs:"
echo "  ${OUT_DIR}/if_else.ssa.koopa"
echo "  ${OUT_DIR}/while_loop.ssa.koopa"
