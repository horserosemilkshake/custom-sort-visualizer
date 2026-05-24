#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-release-windows}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist/windows}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "${BUILD_DIR}" --config Release

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

cp "${BUILD_DIR}/sort-visualizer.exe" "${DIST_DIR}/"
if [[ -f "${BUILD_DIR}/sort-visualizer-worker.exe" ]]; then
  cp "${BUILD_DIR}/sort-visualizer-worker.exe" "${DIST_DIR}/"
fi
cp "${ROOT_DIR}/LICENSE" "${DIST_DIR}/"
cp "${ROOT_DIR}/README.md" "${DIST_DIR}/"

if command -v ntldd >/dev/null 2>&1; then
  while IFS= read -r dll; do
    [[ -z "${dll}" ]] && continue
    [[ -f "${dll}" ]] && cp -n "${dll}" "${DIST_DIR}/"
  done < <(ntldd -R "${BUILD_DIR}/sort-visualizer.exe" | awk '/=> \/mingw64\/bin\// {print $3}' | sort -u)
fi

echo "Windows bundle created in ${DIST_DIR}"
