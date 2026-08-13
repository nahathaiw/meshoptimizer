#!/usr/bin/env bash
set -euo pipefail

# Resolve every path relative to this script so the command works from any
# current working directory and never depends on a developer's home path.
sample_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

if (( $# > 2 )); then
    printf 'Usage: %s [INPUT.ply] [--optimize|--no-optimize]\n' "$0" >&2
    exit 2
fi

input="$sample_dir/input/hotdog.ply"
mode=--optimize
if (( $# >= 1 )); then
    if [[ "$1" == "--optimize" || "$1" == "--no-optimize" ]]; then
        mode=$1
    else
        input=$1
    fi
fi
if (( $# == 2 )); then
    mode=$2
fi
if [[ ! -f "$input" ]]; then
    printf 'Error: input file does not exist: %s\n' "$input" >&2
    exit 2
fi
if [[ "$mode" != "--optimize" && "$mode" != "--no-optimize" ]]; then
    printf 'Error: mode must be --optimize or --no-optimize\n' >&2
    exit 2
fi

build_dir="$sample_dir/build"
output_dir="$sample_dir/output"
results_dir="$sample_dir/results"
mkdir -p "$build_dir" "$output_dir" "$results_dir"

# A matching before/after checksum proves that the runner did not alter the
# original PLY. The codec's losslessness is validated separately by the C++
# program with full byte-for-byte comparisons of decoded buffers.
sha256sum "$input" > "$results_dir/input_checksum_before.txt"

printf '%s\n' '[1/3] Configuring and building the sample...'
if ! cmake -S "$sample_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release > "$results_dir/build.log" 2>&1; then
    cat "$results_dir/build.log" >&2
    exit 1
fi
if ! cmake --build "$build_dir" --config Release --parallel >> "$results_dir/build.log" 2>&1; then
    cat "$results_dir/build.log" >&2
    exit 1
fi
printf '%s\n' '      Build: PASS'

printf '%s\n' '[2/3] Encoding, decoding, and validating HOTDOG...'
"$build_dir/hotdog_lossless" "$input" "$output_dir" "$results_dir" "$mode" | tee "$results_dir/console.txt"

sha256sum "$input" > "$results_dir/input_checksum_after.txt"
before=$(cut -d' ' -f1 "$results_dir/input_checksum_before.txt")
after=$(cut -d' ' -f1 "$results_dir/input_checksum_after.txt")
if [[ "$before" != "$after" ]]; then
    printf '\n[3/3] Original input unchanged: FAIL\n' | tee -a "$results_dir/console.txt" >&2
    exit 1
fi

printf '\n[3/3] Original input unchanged: PASS\n' | tee -a "$results_dir/console.txt"
printf '      SHA-256: %s\n' "$after" | tee -a "$results_dir/console.txt"
printf '\nAll checks passed. Generated files are in SAMPLE-test/output and SAMPLE-test/results.\n' | tee -a "$results_dir/console.txt"
