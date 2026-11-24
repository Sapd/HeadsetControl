#!/usr/bin/env bash
# Checks that the device table in README.md is what `headsetcontrol --readme-helper` prints.
# Usage: check-readme-table.sh [path/to/headsetcontrol] [path/to/README.md]
set -euo pipefail

binary=${1:-build/headsetcontrol}
readme=${2:-README.md}

generated=$("$binary" --readme-helper)
# The table starts at its header row and ends at the first line that is not a table row
in_readme=$(awk '/^\| Device \| Platform \|/ { table = 1 } table && !/^\|/ { exit } table' "$readme")

if [ "$generated" != "$in_readme" ]; then
    echo "::error file=$readme::The device table in $readme differs from the output of 'headsetcontrol --readme-helper'. Run it and replace the table with its output."
    diff -u --label "headsetcontrol --readme-helper" --label "$readme" <(printf '%s\n' "$generated") <(printf '%s\n' "$in_readme") || true
    exit 1
fi
echo "The device table in $readme is up to date."
