#!/usr/bin/env bash

set -euo pipefail

flag_check=false

for arg in "$@"; do
  case $arg in
    --check)
      flag_check=true
      shift
      ;;
    *)
      echo "Error: Unknown argument '$1'" >&2
      exit 1
      ;;
  esac
done


CLANG_PATHS=(
    ./src
)

RUN_CLANG=./scripts/run-clang-format.sh

if [ "$flag_check" = false ]; then
  CLANG_EXTRA_ARGS="--in-place"
else
  CLANG_EXTRA_ARGS=""
fi

# Format C/C++
"$RUN_CLANG" --recursive $CLANG_EXTRA_ARGS "${CLANG_PATHS[@]}"
