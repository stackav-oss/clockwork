#!/usr/bin/env bash
# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

# THIS IS A GENERATED FILE, DO NOT MODIFY IT BY HAND!
# To regenerate it automatically, run:
#   bazel run @@//tools/gazelle:write_gazelle_wrapper

set -euo pipefail

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIRECTORY_PATH="$(dirname "${SCRIPT_PATH}")"
WORKSPACE_ROOT="$(realpath "${SCRIPT_DIRECTORY_PATH}/../..")"

if [ -n "${TMPDIR:-}" ]; then
  WRAPPER_CACHE_DIRECTORY="${TMPDIR}/bazel_binary_wrappers"
else
  WRAPPER_CACHE_DIRECTORY="${WORKSPACE_ROOT}/.cache/bazel_binary_wrappers"
fi
# We include this in the hash path to avoid conflicts with multiple checkouts or worktrees
SCRIPT_PATH_HASH="$(echo -n "${SCRIPT_PATH}" | sha256sum | cut -c1-5)"
EXPECTED_HASH="26c37a2f3c7cc7b0a654f805943a281e23d712ce8f161d330158eb91537bbd9f"
CACHED_APPIMAGE_PATH="${WRAPPER_CACHE_DIRECTORY}/$(basename "$0").${SCRIPT_PATH_HASH}.${EXPECTED_HASH}.appimage"
EXTRACTED_APPIMAGE_PATH="${WRAPPER_CACHE_DIRECTORY}/$(basename "$0").${SCRIPT_PATH_HASH}.${EXPECTED_HASH}.appdir"
EXTRACT_LOCK_DIRECTORY="${WRAPPER_CACHE_DIRECTORY}/locks/$(basename "$0").${SCRIPT_PATH_HASH}.${EXPECTED_HASH}.lock"
APPIMAGE_PATH="${WORKSPACE_ROOT}/bazel-bin/tools/gazelle/write_gazelle_wrapper_generated_appimage"

mkdir --parents "$(dirname "${CACHED_APPIMAGE_PATH}")"
mkdir --parents "$(dirname "${EXTRACT_LOCK_DIRECTORY}")"

# If the cached AppImage doesn't exist, then we need to rebuild it.
if [ ! -x "${CACHED_APPIMAGE_PATH}" ]; then
  TEMP_APPIMAGE_PATH="$(mktemp "${CACHED_APPIMAGE_PATH}.tmp.XXXXXX")"
  on_appimage_cache_exit() {
    rm --force "$TEMP_APPIMAGE_PATH"
  }
  trap "on_appimage_cache_exit" EXIT

  APPIMAGE_PATH="${WORKSPACE_ROOT}/bazel-bin/tools/gazelle/write_gazelle_wrapper_generated_appimage"
  # Build the AppImage target and copy the resulting AppImage into this wrapper's cache. The Bazel commands here
  # will only work within the Bazel workspace, but some callers may try to run this script from outside of it.
  pushd "${WORKSPACE_ROOT}" > /dev/null
  BAZEL_STARTUP_FLAGS_ENV_VAR_NAME=""
  BAZEL_STARTUP_FLAGS=""
  if [[ -n "${BAZEL_STARTUP_FLAGS_ENV_VAR_NAME}" && -n "${!BAZEL_STARTUP_FLAGS_ENV_VAR_NAME:-}" ]]; then
    BAZEL_STARTUP_FLAGS="${!BAZEL_STARTUP_FLAGS_ENV_VAR_NAME}"
  fi
  BAZEL_BUILD_FLAGS_ENV_VAR_NAME=""
  BAZEL_BUILD_FLAGS="$([[ -n "${BAZEL_BUILD_FLAGS_ENV_VAR_NAME}" ]] && echo "${!BAZEL_BUILD_FLAGS_ENV_VAR_NAME:-}" || echo "")"
  # The wrapper needs the AppImage as a local file to copy, even when Bazel is configured for minimal remote downloads.
  eval "bazel ${BAZEL_STARTUP_FLAGS} build --color=yes ${BAZEL_BUILD_FLAGS} --remote_download_outputs=toplevel @@//tools/gazelle:write_gazelle_wrapper_generated_appimage"
  popd > /dev/null

  if [ ! -f "${APPIMAGE_PATH}" ]; then
    echo "Expected Bazel to produce AppImage at ${APPIMAGE_PATH}" >&2
    exit 1
  fi

  cp "${APPIMAGE_PATH}" "${TEMP_APPIMAGE_PATH}"
  chmod +x "${TEMP_APPIMAGE_PATH}"
  mv "${TEMP_APPIMAGE_PATH}" "${CACHED_APPIMAGE_PATH}"
  trap - EXIT
fi

wait_for_extracted_appimage() {
  while [ -d "${EXTRACT_LOCK_DIRECTORY}" ] && [ ! -x "${EXTRACTED_APPIMAGE_PATH}/AppRun" ]; do
    sleep 0.1
  done
}

while [ ! -x "${EXTRACTED_APPIMAGE_PATH}/AppRun" ]; do
  if mkdir "${EXTRACT_LOCK_DIRECTORY}" 2> /dev/null; then
    TEMP_EXTRACT_DIRECTORY="$(mktemp --directory "${WRAPPER_CACHE_DIRECTORY}/$(basename "$0").extract.XXXXXX")"
    on_extract_exit() {
      rm --recursive --force "${TEMP_EXTRACT_DIRECTORY}"
      rmdir "${EXTRACT_LOCK_DIRECTORY}" 2> /dev/null || true
    }
    trap "on_extract_exit" EXIT

    rm --recursive --force "${EXTRACTED_APPIMAGE_PATH}"
    pushd "${TEMP_EXTRACT_DIRECTORY}" > /dev/null
    "${CACHED_APPIMAGE_PATH}" --appimage-extract > /dev/null
    popd > /dev/null

    if [ ! -x "${TEMP_EXTRACT_DIRECTORY}/squashfs-root/AppRun" ]; then
      echo "Expected extracted AppImage to contain AppRun at ${TEMP_EXTRACT_DIRECTORY}/squashfs-root/AppRun" >&2
      exit 1
    fi

    mv "${TEMP_EXTRACT_DIRECTORY}/squashfs-root" "${EXTRACTED_APPIMAGE_PATH}"
    rm --recursive --force "${TEMP_EXTRACT_DIRECTORY}"
    rmdir "${EXTRACT_LOCK_DIRECTORY}"
    trap - EXIT
  else
    wait_for_extracted_appimage
  fi
done

env \
  BUILD_WORKSPACE_DIRECTORY="${BUILD_WORKSPACE_DIRECTORY:-${WORKSPACE_ROOT}}" \
  "${EXTRACTED_APPIMAGE_PATH}/AppRun" "$@"
