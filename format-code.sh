#!/bin/bash
# Helper script to format code with the correct clang-format version
# This ensures consistency with CI

set -e

# Try to find clang-format-18
CLANG_FORMAT=""

if command -v clang-format-18 &> /dev/null; then
    CLANG_FORMAT="clang-format-18"
elif [ -f "/opt/homebrew/opt/llvm@18/bin/clang-format" ]; then
    CLANG_FORMAT="/opt/homebrew/opt/llvm@18/bin/clang-format"
elif [ -f "/usr/local/opt/llvm@18/bin/clang-format" ]; then
    CLANG_FORMAT="/usr/local/opt/llvm@18/bin/clang-format"
else
    echo "Error: clang-format-18 not found!"
    echo "Please install it:"
    echo "  macOS: brew install llvm@18"
    echo "  Linux: sudo apt-get install clang-format-18"
    exit 1
fi

# Check version
VERSION=$($CLANG_FORMAT --version)
echo "Using: $VERSION"

if [[ ! "$VERSION" =~ "version 18" ]]; then
    echo "Warning: Not using version 18! CI uses version 18."
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Format all C and H files
echo "Formatting code..."
find src -name "*.c" -o -name "*.h" | xargs $CLANG_FORMAT -i

echo "Done! All source files formatted."
