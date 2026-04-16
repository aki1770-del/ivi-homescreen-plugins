#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 ivi-homescreen contributors
#
# clang_tidy.sh — run clang-tidy across the plugins/ sources.
#
# Usage:
#   scripts/clang_tidy.sh [BUILD_DIR] [PLUGIN]
#
# BUILD_DIR defaults to "build" (relative paths are resolved against the
# repository root) and must contain a compile_commands.json (configure the
# top-level ivi-homescreen build with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON).
#
# PLUGIN, if given, restricts the run to plugins/<PLUGIN>/ — matching the CI
# reusable workflow behavior.  Without it, every plugin source in
# compile_commands.json is linted.
#
# The environment variable CLANG_TIDY selects the binary (default:
# clang-tidy-18, with a fallback to clang-tidy).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${1:-build}"
PLUGIN="${2:-}"

if [[ "${BUILD_DIR}" != /* ]]; then
    BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}"
fi

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
    echo "ERROR: compile_commands.json not found in ${BUILD_DIR}." >&2
    echo "Build ivi-homescreen first with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON." >&2
    exit 1
fi

if [[ -n "${PLUGIN}" && ! -d "${ROOT_DIR}/plugins/${PLUGIN}" ]]; then
    echo "ERROR: plugin directory not found: ${ROOT_DIR}/plugins/${PLUGIN}" >&2
    exit 1
fi

CLANG_TIDY="${CLANG_TIDY:-clang-tidy-18}"
if ! command -v "${CLANG_TIDY}" &>/dev/null; then
    CLANG_TIDY="clang-tidy"
fi

if ! command -v "${CLANG_TIDY}" &>/dev/null; then
    echo "ERROR: clang-tidy not found. Install it (e.g. sudo apt install clang-tidy-18)." >&2
    exit 1
fi

echo "Using: $(command -v "${CLANG_TIDY}")"

# Only lint files actually compiled in this build configuration (those
# present in compile_commands.json).  Vendored third_party sources under
# any plugin are excluded.
mapfile -t FILES < <(
    python3 - "${BUILD_DIR}/compile_commands.json" "${ROOT_DIR}" "${PLUGIN}" <<'PY'
import json
import os
import sys

cc_path, root, plugin = sys.argv[1], os.path.realpath(sys.argv[2]), sys.argv[3]
with open(cc_path) as fh:
    entries = json.load(fh)

plugins_root = os.path.join(root, "plugins") + os.sep
scope = os.path.join(plugins_root, plugin) + os.sep if plugin else plugins_root

seen = set()
for entry in entries:
    path = os.path.realpath(
        entry["file"]
        if os.path.isabs(entry["file"])
        else os.path.join(entry.get("directory", ""), entry["file"])
    )
    if not path.startswith(scope):
        continue
    # Exclude vendored sources under any third_party/ directory.
    rel = os.path.relpath(path, plugins_root)
    if any(part == "third_party" for part in rel.split(os.sep)):
        continue
    if not path.endswith((".cc", ".cpp")):
        continue
    if path in seen:
        continue
    seen.add(path)
    print(path)
PY
)

if [[ ${#FILES[@]} -eq 0 ]]; then
    scope_msg="plugins/"
    [[ -n "${PLUGIN}" ]] && scope_msg="plugins/${PLUGIN}/"
    echo "ERROR: no ${scope_msg} source files found in compile_commands.json." >&2
    exit 1
fi

printf '%s\n' "${FILES[@]}" | sort | \
    xargs "${CLANG_TIDY}" -p "${BUILD_DIR}" \
        --warnings-as-errors='*,-bugprone-macro-parentheses' 2>&1

echo "clang-tidy passed."
