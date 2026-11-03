#!/usr/bin/env bash
set -eu

repo_root="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

venv_dir="${VIRTUAL_ENV:-$repo_root/ramulator2-venv}"

if [ -d ".venv" ] && [ ! -d "$venv_dir" ]; then
  mv .venv "$venv_dir"
fi

if [ ! -d "$venv_dir" ]; then
  python3 -m venv "$venv_dir"
fi

. "$venv_dir/bin/activate"
python -m pip install --upgrade pip
python -m pip install -r requirements-dev.txt
python -m pip install -e .

# Build the simulator core and the trace visualizer, so notebooks and examples run right away.
# The visualizer's example traces are recorded with the simulator, before the visualizer build.
./build.sh
python visualizer/scripts/gen_example_traces.py
(cd visualizer && npm ci --no-audit --no-fund && npm run build)
