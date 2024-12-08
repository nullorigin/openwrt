#!/bin/sh

# Copyright (C) 2002 Erik Andersen <andersen@codepoet.org>
# SPDX-License-Identifier: GPL-2.0

# Set directories from arguments, or use defaults.
target_dir=${1-.}
patch_dir=${2-../kernel-patches}
patch_pattern=${3-*}

if [ ! -d "${target_dir}" ] ; then
	echo "Aborting.  '${target_dir}' is not a directory."
	exit 1
fi

if [ ! -d "${patch_dir}" ] ; then
	echo "Aborting.  '${patch_dir}' is not a directory."
	exit 1
fi

for patch in ${patch_dir}/${patch_pattern} ; do
	case "${patch}" in
		*.gz)
			compression="gzip"
			decompress="gunzip -dc"
			;;
		*.bz)
			compression="bzip"
			decompress="bunzip -dc"
			;;
		*.bz2)
			compression="bzip2"
			decompress="bunzip2 -dc"
			;;
		*.zip)
			compression="zip"
			decompress="unzip -d"
			;;
		*.Z)
			compression="compress"
			decompress="uncompress -c"
			;;
		*)
			compression="plaintext"
			decompress="cat"
			;;
	esac

	echo ""
	echo "Applying ${patch} using ${compression}: "
	${decompress} "${patch}" | ${PATCH:-patch} -f -p1 -d "${target_dir}"
	if [ $? != 0 ] ; then
		echo "Patch failed!  Please fix ${patch}!"
		exit 1
	fi
done

# Check for rejects...
if [ "$(find "${target_dir}" '(' -name '*.rej' -o -name '.*.rej' ')' -print)" ] ; then
	echo "Aborting.  Reject files found."
	exit 1
fi

# Remove backup files
find "${target_dir}" '(' -name '*.orig' -o -name '.*.orig' ')' -exec rm -f {} \;
