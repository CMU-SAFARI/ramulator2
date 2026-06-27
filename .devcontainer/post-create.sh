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
