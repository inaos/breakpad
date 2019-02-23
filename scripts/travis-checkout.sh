#!/bin/bash
set -ex

get_depot_tools() {
  cd
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
  PATH="$HOME/depot_tools:$PATH"
}

gclient_sync() {
  # Rename the source dir to match what gclient expects.
  #srcdir=$(basename "$APPVEYOR_BUILD_FOLDER")
  #cd "${APPVEYOR_BUILD_FOLDER}"/..
  #mv "${srcdir}" src
  cd $APPVEYOR_BUILD_FOLDER\..
  gclient config --unmanaged https://github.com/google/breakpad.git
  gclient sync
}

main() {
  get_depot_tools
  gclient_sync
}

main "$@"
