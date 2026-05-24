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
# Avoid bundling host-provided platform and GTK stack libraries.
# This reduces ABI-mismatch crashes caused by mixing many transitive libs in AppImage.
EXCLUDE_LIBS=(
  "libsystemd.so*"
  "libmount.so*"
  "libblkid.so*"
  "libselinux.so*"
  "libseccomp.so*"
  "libdbus-1.so*"
  "libglib-2.0.so*"
  "libgobject-2.0.so*"
  "libgio-2.0.so*"
  "libgmodule-2.0.so*"
  "libgtk-3.so*"
  "libgdk-3.so*"
  "libgdk_pixbuf-2.0.so*"
  "libatk-1.0.so*"
  "libatk-bridge-2.0.so*"
  "libatspi.so*"
  "libpango-1.0.so*"
  "libpangocairo-1.0.so*"
  "libpangoft2-1.0.so*"
  "libcairo.so*"
  "libcairo-gobject.so*"
  "libepoxy.so*"
  "libxkbcommon.so*"
  "libthai.so*"
  "libdatrie.so*"
  "libjson-glib-1.0.so*"
  "libsqlite3.so*"
  "liblcms2.so*"
  "libxml2.so*"
  "libffi.so*"
  "libgraphite2.so*"
  "libpixman-1.so*"
)

LINUXDEPLOY_ARGS=()
for pattern in "${EXCLUDE_LIBS[@]}"; do
  LINUXDEPLOY_ARGS+=("--exclude-library" "${pattern}")
done

ARCH=x86_64 NO_STRIP=1 "${LINUXDEPLOY_BIN}" \
  --appdir "${APPDIR}" \
  "${LINUXDEPLOY_ARGS[@]}" \
  -d "packaging/sort-visualizer.desktop" \
  -i "assets/sort-visualizer.svg" \
  --output appimage

mv -f ./*.AppImage "${DIST_DIR}/"
popd >/dev/null

echo "AppImage created in ${DIST_DIR}"
