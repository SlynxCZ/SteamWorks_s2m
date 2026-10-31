#!/bin/bash
set -e

### --- Version -------------------------------------------------------------
if [ -n "${SEMVER:-}" ]; then
  export SEMVER
  echo "=== Version: $SEMVER (from the environment) ==="
elif SEMVER="$(git describe --tags --exact-match 2>/dev/null)"; then
  export SEMVER
  echo "=== Version: $SEMVER (tag on HEAD) ==="
else
  unset SEMVER
  echo '=== Version: no tag on HEAD, building as "Local" ==='
fi
export GITHUB_SHA_SHORT="$(git rev-parse --short HEAD)"

### --- SDKs ----------------------------------------------------------------
# A Source2Mod plugin builds against the Source2Mod SDK, which carries the
# s2sdk it needs, game protobufs included (its vendor/s2sdk submodule,
# brought in by --recursive).
SDK_DIR="/tmp/sdk"
SOURCE2MODSDK_DIR="$SDK_DIR/source2mod-sdk"

rm -rf "$SDK_DIR"
mkdir -p "$SDK_DIR"
echo "=== Downloading Source2Mod-SDK ==="
git clone --recursive https://github.com/Source2Mod/source2mod-sdk.git "$SOURCE2MODSDK_DIR"

export SOURCE2MOD_SDK="$SOURCE2MODSDK_DIR"

### --- Build (AMBuild) ----------------------------------------------------
# s2sdk and s2sdk-manifests both come from the SDK's submodules
# (vendor/s2sdk, vendor/s2sdk-manifests).
rm -rf build
mkdir build
cd build
CC=clang-18 CXX=clang++-18 python3 ../configure.py --enable-optimize --sdks cs2 --targets x86_64
ambuild

### --- Package -------------------------------------------------------------
# AMBuild wrote package/cs2/addons/source2mod/plugins/steamworks.smx;
