#!/bin/sh

# directory where search for images
TOP_DIR="${TOP_DIR:-./bin/targets}"
# key to sign images
BUILD_KEY="${BUILD_KEY:-key-build}"
# remove other signatures (added e.g.  by buildbot)
REMOVE_OTER_SIGNATURES="${REMOVE_OTER_SIGNATURES:-1}"

# find all sysupgrade images in TOP_DIR
# factory images don't need signatures as non OpenWrt system doesn't check them anyway
images=$(find "$TOP_DIR" -type f -name "*-sysupgrade.bin" -not -name "*-factory.bin")
for image in $images; do
	# check if image actually support metadata
	if fwtool -i /dev/null "$image"; then
		# remove all previous signatures and sign
		[ -n "$REMOVE_OTER_SIGNATURES" ] && fwtool -t -s /dev/null "$image" || true
		usign -S -m "$image" -s "$BUILD_KEY" -x "$image.sig" || continue
		ucert -A -c "$image.ucert" -x "$image.sig" || continue
		fwtool -S "$image.ucert" "$image"
	fi
done
