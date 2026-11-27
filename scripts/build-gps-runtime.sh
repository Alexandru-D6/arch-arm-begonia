#!/bin/bash
# Build only our adapters; stock Android binaries stay on the user's handset.
set -euo pipefail
ndk=${1:?Usage: build-gps-runtime.sh ANDROID_NDK OUTPUT_DIRECTORY}
output=${2:?Usage: build-gps-runtime.sh ANDROID_NDK OUTPUT_DIRECTORY}
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_dir=$script_dir/../rootfs/gps
compiler=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin
mkdir -p "$output"
"$compiler/armv7a-linux-androideabi30-clang" -shared -fPIC -O2 -Wall -Wextra -Werror \
    "$source_dir/begonia-gps-properties.c" -o "$output/begonia-gps-properties32.so"
"$compiler/aarch64-linux-android30-clang" -shared -fPIC -O2 -Wall -Wextra -Werror \
    "$source_dir/begonia-gps-properties.c" -o "$output/begonia-gps-properties64.so"
"$compiler/aarch64-linux-android30-clang" -O2 -Wall -Wextra -Werror \
    "$source_dir/begonia-gps-nmea.c" -ldl -o "$output/begonia-gps-nmea"
