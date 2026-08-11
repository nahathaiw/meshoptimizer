#!/usr/bin/env bash
set -euo pipefail

sample_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repository_dir=$(cd -- "$sample_dir/.." && pwd)

# Refuse to remove anything unless this script is located exactly in the sample
# directory beneath a meshoptimizer repository. This keeps cleanup narrowly
# scoped even if the script is launched from another working directory.
if [[ "$sample_dir" != "$repository_dir/SAMPLE-test" || ! -f "$repository_dir/src/meshoptimizer.h" ]]; then
    printf 'Safety check failed; no files were removed.\n' >&2
    exit 1
fi

for name in build output results; do
    target="$sample_dir/$name"
    if [[ -d "$target" ]]; then
        printf 'Removing generated directory: %s\n' "$target"
        rm -rf -- "$target"
    fi
done

printf 'SAMPLE-test generated files are clean.\n'
