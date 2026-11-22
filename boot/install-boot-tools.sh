#!/bin/bash
# Install matching AOSP pack/unpack tools for the GitHub Actions build.
set -euo pipefail
tools="$RUNNER_TEMP/mkbootimg"
git clone --quiet --depth 1 --branch android-15.0.0_r1 \
    https://android.googlesource.com/platform/system/tools/mkbootimg "$tools"
for tool in mkbootimg unpack_bootimg; do
    ln -s "$tool.py" "$tools/$tool"
done
echo "$tools" >> "$GITHUB_PATH"
