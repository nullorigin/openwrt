#!/usr/bin/env bash

INPUT_FILE="$1"
TARGET_DIR="$2"

if [ -z "$INPUT_FILE" ] || [ -z "$TARGET_DIR" ]; then
	echo "Usage: $0 <file> <directory>"
	exit 1
fi

if [ ! -f "$INPUT_FILE" ] || [ ! -d "$TARGET_DIR" ]; then
	echo "File/directory not found"
	exit 1
fi

cd "$TARGET_DIR" || exit
xargs -r -0 rm -f < "$INPUT_FILE"
xargs -r -0 rmdir < "$INPUT_FILE"
