#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-release-linux}"
APPDIR="${APPDIR:-${ROOT_DIR}/AppDir}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist}"

mkdir -p "${DIST_DIR}"
rm -rf "${APPDIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DBUILD_TESTING=OFF

cmake --build "${BUILD_DIR}" --config Release

DESTDIR="${APPDIR}" cmake --install "${BUILD_DIR}" --config Release

mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/scalable/apps"
cp "${ROOT_DIR}/packaging/sort-visualizer.desktop" "${APPDIR}/usr/share/applications/sort-visualizer.desktop"
cp "${ROOT_DIR}/assets/sort-visualizer.svg" "${APPDIR}/usr/share/icons/hicolor/scalable/apps/sort-visualizer.svg"

LINUXDEPLOY_BIN="${ROOT_DIR}/.tools/linuxdeploy-x86_64.AppImage"
if [[ ! -x "${LINUXDEPLOY_BIN}" ]]; then
  mkdir -p "${ROOT_DIR}/.tools"
  curl -L "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" -o "${LINUXDEPLOY_BIN}"
  chmod +x "${LINUXDEPLOY_BIN}"
fi

pushd "${ROOT_DIR}" >/dev/null
ARCH=x86_64 NO_STRIP=1 "${LINUXDEPLOY_BIN}" \
  --appdir "${APPDIR}" \
  -d "packaging/sort-visualizer.desktop" \
  -i "assets/sort-visualizer.svg" \
  --output appimage

mv -f ./*.AppImage "${DIST_DIR}/"
popd >/dev/null

echo "AppImage created in ${DIST_DIR}"
