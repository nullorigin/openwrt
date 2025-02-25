#!/usr/bin/env bash

DIR="$1"

if [ -d "$DIR" ]; then
	DIR="$(cd "$DIR" || exit; pwd)"
else
	echo "Usage: $0 toolchain-dir"
	exit 1
fi

echo -n "Locating cpp ... "
CPP="$(find "$DIR"/{bin,usr/bin,usr/local/bin} -name '*-cpp' | head -1)"
if [ ! -x "$CPP" ]; then
	echo "Can't locate a cpp executable in '$DIR' !"
	exit 1
fi

patch_specs() {
	local specs="$1/specs"

	if [ -f "$specs" ]; then
		grep -qs "STAGING_DIR" "$specs" || return 0
		rm -f "$specs"
	fi

	STAGING_DIR="$DIR" "$CPP" -dumpspecs | awk '
		/^ *cpp:/ {
			$0 = $0 " -idirafter %:getenv(STAGING_DIR /usr/include)"
		}
		/^ *link.*:/ {
			sub(/(%@?\{L.\})/, "& -L %:getenv(STAGING_DIR /usr/lib) -rpath-link %:getenv(STAGING_DIR /usr/lib)")
		}
		{ print $0 }
	' > "$specs"
	return 0
}

VERSION="$(STAGING_DIR="$DIR" "$CPP" --version | sed -ne 's/^.* (.*) //; s/ .*$//; 1p')"
VERSION="${VERSION:-unknown}"

case "${VERSION##* }" in
	2.*|3.*|4.0.*|4.1.*|4.2.*)
		echo "The compiler version does not support getenv() in spec files."
		echo -n "Wrapping binaries instead ... "
		if "${0%/*}/ext-toolchain.sh" --toolchain "$DIR" --wrap "${CPP%/*}"; then
			echo "ok" && exit 0
		else
			status=$?
			echo "failed" && exit $status
		fi
	;;
	*)
		patch_specs "$DIR/$(${CPP##*/} -print-file-name=libstdc++.so)"
		echo "Toolchain successfully patched."
		exit 0
	;;
esac
