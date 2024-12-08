#!/bin/sh
if [ $# -lt 4 ]; then
    echo "Usage: $0 <cross> <reference> <pic .a> <destination>"
    echo "  <cross>   The cross-compiler prefix"
    echo "  <reference>  The reference library whose symbols are to be preserved"
    echo "  <pic .a>   The input library which is to be linked against"
    echo "  <destination>  The destination library name"
    exit 1
fi

cross="$1"; shift
ref="$1"; shift
pic="$1"; shift
dest="$1"; shift

"${cross}"gcc -nostdlib -nostartfiles -shared -Wl,--gc-sections -o "$dest" \
    "$("${cross}"nm "$ref" | grep -E '........ [TW] ' | awk '$3{printf "-u%s ", $3}')" "$pic" "$@"
