#!/usr/bin/env bash
# Fetch the pinned host support used by the Meshtastic SDL simulator.
set -euo pipefail
script_dir="$(cd "$(dirname "$0")" && pwd)"
source_dir="$script_dir/.deps/wadamesh"
revision=26578cdc87896c78d533c00568846ecdd7dcfb2b
if [[ ! -d "$source_dir/.git" ]]; then
	mkdir -p "$source_dir"
	git -C "$source_dir" init -q
	git -C "$source_dir" remote add origin https://github.com/ALLFATHER-BV/wadamesh.git
fi
if [[ "$(git -C "$source_dir" rev-parse --verify HEAD 2>/dev/null || true)" != "$revision" ]]; then
	if [[ -n "$(git -C "$source_dir" status --porcelain)" ]]; then
		echo "Host-support checkout has local changes: $source_dir" >&2
		exit 1
	fi
	git -C "$source_dir" fetch --depth 1 origin "$revision"
	git -C "$source_dir" checkout --detach "$revision"
fi
bash "$source_dir/web/fetch-deps.sh"
