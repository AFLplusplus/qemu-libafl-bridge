#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
ROOT_DIR="$SCRIPT_DIR/.."

if [ "$1" = "check" ]; then
    CHECK="--dry-run --Werror"
elif [ "$1" != "" ]; then
    echo "Unknown option: $1. Type 'check' to check without modifying any file; do not provide any argument to format automatically."
    exit 1
fi

cd "$SCRIPT_DIR" || exit 1
find "$ROOT_DIR/libafl" -name "*.c" -exec clang-format $CHECK -style="file:$ROOT_DIR/libafl/.clang-format" -i {} \;
find "$ROOT_DIR/include/libafl" -name "*.h" -exec clang-format $CHECK -style=file:"$ROOT_DIR/libafl/.clang-format" -i {} \;
