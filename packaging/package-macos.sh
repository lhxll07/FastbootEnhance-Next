#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${FASTBOOT_ENHANCE_BUILD_DIR:-${project_root}/build-macos}"
platform_tools="${FASTBOOT_ENHANCE_PLATFORM_TOOLS:-}"
runtime_dir="${FASTBOOT_ENHANCE_RUNTIME_DIR:-}"
dist_dir="${project_root}/dist"
stage_dir="${dist_dir}/macos-stage"
app_path="${stage_dir}/FastbootEnhance.app"
output="${FASTBOOT_ENHANCE_OUTPUT:-${dist_dir}/FastbootEnhance-macOS-$(uname -m).dmg}"

if [[ -z "${platform_tools}" || ! -x "${platform_tools}/fastboot" || ! -x "${platform_tools}/adb" ]]; then
    printf '%s\n' 'Set FASTBOOT_ENHANCE_PLATFORM_TOOLS to a directory containing fastboot and adb.' >&2
    exit 1
fi

macdeployqt="${FASTBOOT_ENHANCE_MACDEPLOYQT:-}"
if [[ -z "${macdeployqt}" ]]; then
    macdeployqt="$(command -v macdeployqt 2>/dev/null || true)"
fi
if [[ -z "${macdeployqt}" || ! -x "${macdeployqt}" ]]; then
    printf '%s\n' 'macdeployqt was not found. Set FASTBOOT_ENHANCE_MACDEPLOYQT.' >&2
    exit 1
fi

cmake --build "${build_dir}" --config Release --parallel
rm -rf "${stage_dir}"
mkdir -p "${dist_dir}" "${stage_dir}"
cp -R "${build_dir}/FastbootEnhance.app" "${stage_dir}/"
mkdir -p "${app_path}/Contents/Resources/bin"
cp -R "${platform_tools}/." "${app_path}/Contents/Resources/bin/"
chmod +x "${app_path}/Contents/Resources/bin/fastboot" "${app_path}/Contents/Resources/bin/adb"

"${macdeployqt}" "${app_path}" -always-overwrite -no-translations

if [[ -n "${runtime_dir}" ]]; then
    mkdir -p "${app_path}/Contents/Frameworks"
    find "${runtime_dir}" -maxdepth 1 -type f -name '*.dylib' -exec cp -f {} "${app_path}/Contents/Frameworks/" \;
fi

dylibbundler_bin="$(command -v dylibbundler 2>/dev/null || true)"
if [[ -z "${dylibbundler_bin}" ]]; then
    printf '%s\n' 'dylibbundler was not found; refusing to create a non-portable DMG.' >&2
    exit 1
fi
dylibbundler -od -b -x "${app_path}/Contents/MacOS/FastbootEnhance" \
    -d "${app_path}/Contents/Frameworks"

if [[ -n "${MACOS_SIGN_IDENTITY:-}" ]]; then
    codesign --deep --force --options runtime --sign "${MACOS_SIGN_IDENTITY}" "${app_path}"
else
    printf '%s\n' 'Warning: MACOS_SIGN_IDENTITY is not set; the DMG will not be notarized.' >&2
fi
if [[ -n "${MACOS_NOTARY_PROFILE:-}" && -z "${MACOS_SIGN_IDENTITY:-}" ]]; then
    printf '%s\n' 'MACOS_NOTARY_PROFILE requires MACOS_SIGN_IDENTITY.' >&2
    exit 1
fi

rm -f "${output}"
hdiutil create -volname "Fastboot Enhance" -srcfolder "${app_path}" -ov -format UDZO "${output}" >/dev/null
if [[ -n "${MACOS_DMG_SIGN_IDENTITY:-}" ]]; then
    codesign --force --sign "${MACOS_DMG_SIGN_IDENTITY}" "${output}"
fi
if [[ -n "${MACOS_NOTARY_PROFILE:-}" ]]; then
    xcrun notarytool submit "${output}" --keychain-profile "${MACOS_NOTARY_PROFILE}" --wait
    xcrun stapler staple "${output}"
fi
rm -rf "${stage_dir}"
printf 'Created %s\n' "${output}"
