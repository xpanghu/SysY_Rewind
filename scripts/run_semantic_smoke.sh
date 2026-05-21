#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
COMPILER="${BUILD_DIR}/compiler"
OUT_DIR="${ROOT_DIR}/tmp/semantic-smoke"

mkdir -p "${OUT_DIR}"

cmake --build "${BUILD_DIR}" -j12 -- -s

expect_success() {
  local name="$1"
  local source="$2"
  local input="${OUT_DIR}/${name}.sysy"
  local output="${OUT_DIR}/${name}.koopa"

  printf '%s\n' "${source}" > "${input}"
  "${COMPILER}" -koopa "${input}" -o "${output}"
}

expect_failure() {
  local name="$1"
  local expected="$2"
  local source="$3"
  local input="${OUT_DIR}/${name}.sysy"
  local output="${OUT_DIR}/${name}.koopa"
  local stderr="${OUT_DIR}/${name}.stderr"

  printf '%s\n' "${source}" > "${input}"
  if "${COMPILER}" -koopa "${input}" -o "${output}" 2> "${stderr}"; then
    echo "expected semantic failure for ${name}, but compilation succeeded" >&2
    return 1
  fi

  if ! grep -Fq "${expected}" "${stderr}"; then
    echo "semantic failure for ${name} did not contain expected text" >&2
    echo "expected: ${expected}" >&2
    echo "stderr:" >&2
    cat "${stderr}" >&2
    return 1
  fi
}

expect_success "runtime_array_init" \
'int main() {
  int b = 3;
  int a[3] = {1, 2, b};
  return a[2];
}'

expect_success "array_decay_call" \
'int first(int a[]) {
  return a[0];
}

int main() {
  int a[2] = {4, 5};
  return first(a);
}'

expect_failure "assign_const_scalar" "a is constant" \
'int main() {
  const int a = 1;
  a = 2;
  return a;
}'

expect_failure "assign_const_array" "a is const" \
'int main() {
  const int a[2] = {1, 2};
  a[0] = 3;
  return a[0];
}'

expect_failure "call_scalar_as_array" "actual param type not match formal param" \
'void sink(int a[]) {
}

int main() {
  int x = 0;
  sink(x);
  return 0;
}'

expect_failure "return_value_from_void" "void function should not return a value" \
'void f() {
  return 1;
}

int main() {
  return 0;
}'

expect_failure "break_outside_loop" "break used outside while" \
'int main() {
  break;
  return 0;
}'

echo "semantic smoke outputs:"
echo "  ${OUT_DIR}"
