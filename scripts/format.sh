#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 ivi-homescreen contributors
#
# format.sh — apply clang-format in-place to C++ source files under plugins/.
#
# Usage:
#   scripts/format.sh [--check] [PLUGIN]
#
# Without arguments: formats all *.cc / *.cpp / *.h / *.hpp files under
# plugins/ (excluding any third_party/ directory) using the project's
# .clang-format configuration.
#
# With --check: runs in dry-run mode and exits non-zero if any file would be
# reformatted.  This is the mode called by the CI lint workflow.
#
# With PLUGIN (a directory name under plugins/): limits the operation to
# that single plugin, matching the CI reusable workflow behavior.

set -euo pipefail

CLANG_FORMAT="${CLANG_FORMAT:-clang-format-18}"

if ! command -v "${CLANG_FORMAT}" &>/dev/null; then
    CLANG_FORMAT="clang-format"
fi

if ! command -v "${CLANG_FORMAT}" &>/dev/null; then
    echo "error: clang-format not found. Install it (e.g. sudo apt install clang-format-18)." >&2
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

CHECK_MODE=0
PLUGIN=""
for arg in "$@"; do
    case "${arg}" in
        --check) CHECK_MODE=1 ;;
        -*)
            echo "error: unknown option: ${arg}" >&2
            exit 2
            ;;
        *)
            if [[ -n "${PLUGIN}" ]]; then
                echo "error: only one PLUGIN argument is allowed" >&2
                exit 2
            fi
            PLUGIN="${arg}"
            ;;
    esac
done

SEARCH_ROOT="${REPO_ROOT}/plugins"
if [[ -n "${PLUGIN}" ]]; then
    SEARCH_ROOT="${REPO_ROOT}/plugins/${PLUGIN}"
    if [[ ! -d "${SEARCH_ROOT}" ]]; then
        echo "error: plugin directory not found: ${SEARCH_ROOT}" >&2
        exit 1
    fi
fi

mapfile -d '' FILES < <(
    find "${SEARCH_ROOT}" \
        -type d -name third_party -prune -o \
        -type f \( -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
        -print0
)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No C++ source files found."
    exit 0
fi

if [[ ${CHECK_MODE} -eq 1 ]]; then
    echo "Checking formatting of ${#FILES[@]} file(s) with ${CLANG_FORMAT} (dry-run) ..."
    "${CLANG_FORMAT}" --dry-run -Werror "${FILES[@]}"
    echo "All files are correctly formatted."
else
    echo "Formatting ${#FILES[@]} file(s) with ${CLANG_FORMAT} ..."
    "${CLANG_FORMAT}" -i "${FILES[@]}"
    echo "Done."
fi
