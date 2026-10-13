#!/bin/sh
#
# Compiles and runs the host-side diagnostics and PowerPlay override policy tests.
#
set -eu

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(dirname -- "$script_dir")"
out_dir="${TMPDIR:-/tmp}/nootrx-diag-policy-tests"
mkdir -p "$out_dir"

clang++ -std=c++17 -Wall -Wextra -Werror -O2 -o "$out_dir/DiagPolicyTests" "$repo_root/Tests/DiagPolicyTests.cpp"
"$out_dir/DiagPolicyTests"
