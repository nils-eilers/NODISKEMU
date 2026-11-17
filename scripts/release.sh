#!/usr/bin/env bash
# Build and package firmware using the project's own configuration and metadata.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$root"

usage() {
    cat <<HELP
Usage: $(basename -- "$0") DEVICE
       $(basename -- "$0") -h

Clean, build and package firmware for configs/config-DEVICE.
Output: releases/DEVICE-VERSION.zip (VERSION: major.minor[.patch]).
The ZIP contains the HEX and BIN firmware.
HELP
}

fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }

if [[ $# == 1 && $1 == -h ]]; then
    usage
    exit 0
fi
if [[ $# != 1 ]]; then
    usage >&2
    exit 2
fi
device=$1
[[ $device =~ ^[[:alnum:]][[:alnum:]_.+-]*$ && -f configs/config-$device ]] ||
    fail "Unknown device '$device'. Use -h to list configurations."
config=configs/config-$device
for tool in make perl zip; do
    command -v "$tool" >/dev/null || fail "Required command not found: $tool"
done
# Use the same parser as Makefile, including its MCU prefix and name overrides.
confdata=$(perl scripts/configparser.pl --confdata "$config")
suffix=${confdata%% *}
objdir=obj-$suffix
[[ $suffix =~ ^[[:alnum:]][[:alnum:]_.+-]*$ ]] || fail 'Unsupported object directory name.'

# Generate metadata separately so unsupported targets fail before cleaning/building.
staging=$(mktemp -d)
trap 'rm -rf -- "$staging"' EXIT
perl scripts/configparser.pl --genfiles --makeinc "$staging/make.inc" \
    --header "$staging/autoconf.h" "$config"
arch=$(sed -n 's/^CONFIG_ARCH=//p' "$staging/make.inc")
bootloader=$(sed -n 's/^CONFIG_BOOTLOADER=//p' "$staging/make.inc")
[[ $arch == avr ]] || fail "Device '$device' is not an AVR target."
[[ $bootloader == y ]] || fail "Device '$device' has no bootloader image configured."

# Read the version from make so release names match the firmware.
version=$(make --no-print-directory -s -f scripts/Makefile.main -f - \
    "OBJDIR=$staging" "CONFIG=$config" "CONFIGSUFFIX=$suffix" release-metadata <<'MAKE'
.PHONY: release-metadata
release-metadata:
	@printf '%s\n' '$(PROGRAMVERSION)'
MAKE
)
[[ $version =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]] ||
    fail "Release version '$version' must have the form major.minor[.patch]."
printf 'Building %s version %s...\n' "$device" "$version"
make "CONFIG=$config" clean
make "CONFIG=$config"
for file in NODISKEMU.hex NODISKEMU.bin; do
    [[ -s $objdir/$file ]] || fail "Build output missing or empty: $objdir/$file"
    cp -- "$objdir/$file" "$staging/$file"
done

# Create a fresh ZIP, then replace the destination only after successful packing.
mkdir -p -- releases
archive=$device-$version.zip
(cd -- "$staging" && zip -q "$archive" NODISKEMU.hex NODISKEMU.bin)
mv -f -- "$staging/$archive" "releases/$archive"
printf 'Release created: %s/releases/%s\n' "$root" "$archive"
