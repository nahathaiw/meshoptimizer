#!/usr/bin/env bash
set -euo pipefail

sample_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
python_env="$sample_dir/build/python-env"

if [[ ! -x "$python_env/bin/python" ]]; then
    printf '%s\n' '      Creating isolated Python environment for Trimesh...'
    python3 -m venv "$python_env"
fi

if ! "$python_env/bin/python" -c 'import numpy, trimesh' >/dev/null 2>&1; then
    printf '%s\n' '      Installing pinned NumPy and Trimesh dependencies...'
    if ! "$python_env/bin/python" -m pip install -r "$sample_dir/requirements.txt" > "$sample_dir/results/python-setup.log" 2>&1; then
        cat "$sample_dir/results/python-setup.log" >&2
        exit 1
    fi
fi

"$python_env/bin/python" -c 'import numpy, trimesh; print("      Python validation: NumPy " + numpy.__version__ + ", Trimesh " + trimesh.__version__)'
