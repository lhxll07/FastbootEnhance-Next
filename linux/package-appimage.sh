#!/usr/bin/env bash
set -euo pipefail

export APPIMAGE_EXTRACT_AND_RUN=1

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${FASTBOOT_ENHANCE_BUILD_DIR:-${project_root}/build}"
app_dir="${project_root}/AppDir"
dist_dir="${project_root}/dist"
tools_dir="${project_root}/.build-tools"
linuxdeploy="${tools_dir}/linuxdeploy-x86_64.AppImage"
qt_plugin="${tools_dir}/linuxdeploy-plugin-qt-x86_64.AppImage"
appimagetool="${tools_dir}/appimagetool-x86_64.AppImage"
qt_plugins_dir="${tools_dir}/qt-plugins"
runtime_file="${FASTBOOT_ENHANCE_APPIMAGE_RUNTIME:-}"
qt_query_bin="${FASTBOOT_ENHANCE_QMAKE_BIN:-$(command -v qmake6 2>/dev/null || command -v qmake 2>/dev/null || true)}"
qt_system_plugins="${FASTBOOT_ENHANCE_QT_SYSTEM_PLUGINS:-}"

if [[ -z "${qt_system_plugins}" && -n "${qt_query_bin}" ]]; then
    qt_system_plugins="$(${qt_query_bin} -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
fi
if [[ -z "${qt_system_plugins}" ]]; then
    for candidate in /usr/lib/qt6/plugins /usr/lib64/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins; do
        if [[ -d "${candidate}" ]]; then
            qt_system_plugins="${candidate}"
            break
        fi
    done
fi
if [[ -z "${qt_system_plugins}" || ! -d "${qt_system_plugins}" ]]; then
    printf '%s\n' '找不到 Qt 插件目录。请设置 FASTBOOT_ENHANCE_QT_SYSTEM_PLUGINS。' >&2
    exit 1
fi

download_tool() {
    local target="$1"
    local url="$2"
    if [[ ! -x "${target}" ]]; then
        mkdir -p "$(dirname "${target}")"
        curl -fL --retry 3 --output "${target}" "${url}"
        chmod +x "${target}"
    fi
}

download_tool "${linuxdeploy}" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
download_tool "${qt_plugin}" "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
download_tool "${appimagetool}" "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
ln -sf "${qt_plugin}" "${tools_dir}/linuxdeploy-plugin-qt"
export PATH="${tools_dir}:${PATH}"

rm -rf "${qt_plugins_dir}"
mkdir -p "${qt_plugins_dir}/platforms" "${qt_plugins_dir}/platforminputcontexts" "${qt_plugins_dir}/imageformats"
wayland_plugin_dirs=(
    wayland-decoration-client
    wayland-graphics-integration-client
    wayland-graphics-integration-server
    wayland-shell-integration
)
for plugin_dir in "${wayland_plugin_dirs[@]}"; do
    mkdir -p "${qt_plugins_dir}/${plugin_dir}"
    for plugin in "${qt_system_plugins}/${plugin_dir}"/*.so; do
        [[ -e "${plugin}" ]] || continue
        install -Dm755 "${plugin}" "${qt_plugins_dir}/${plugin_dir}/$(basename "${plugin}")"
    done
done
for plugin in libqxcb.so libqwayland.so; do
    if [[ -e "${qt_system_plugins}/platforms/${plugin}" ]]; then
        install -Dm755 "${qt_system_plugins}/platforms/${plugin}" "${qt_plugins_dir}/platforms/${plugin}"
    fi
done
for plugin in libcomposeplatforminputcontextplugin.so libfcitx5platforminputcontextplugin.so; do
    if [[ -e "${qt_system_plugins}/platforminputcontexts/${plugin}" ]]; then
        install -Dm755 "${qt_system_plugins}/platforminputcontexts/${plugin}" "${qt_plugins_dir}/platforminputcontexts/${plugin}"
    fi
done
for plugin in libqgif.so libqico.so libqjpeg.so libqsvg.so; do
    if [[ -e "${qt_system_plugins}/imageformats/${plugin}" ]]; then
        install -Dm755 "${qt_system_plugins}/imageformats/${plugin}" "${qt_plugins_dir}/imageformats/${plugin}"
    fi
done

cmake -S "${project_root}" -B "${build_dir}" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" --parallel

if [[ -n "${FASTBOOT_ENHANCE_PLATFORM_TOOLS:-}" ]]; then
    fastboot_bin="${FASTBOOT_ENHANCE_FASTBOOT:-${FASTBOOT_ENHANCE_PLATFORM_TOOLS}/fastboot}"
    adb_bin="${FASTBOOT_ENHANCE_ADB:-${FASTBOOT_ENHANCE_PLATFORM_TOOLS}/adb}"
else
    fastboot_bin="${FASTBOOT_ENHANCE_FASTBOOT:-$(command -v fastboot || true)}"
    adb_bin="${FASTBOOT_ENHANCE_ADB:-$(command -v adb || true)}"
fi
if [[ -z "${fastboot_bin}" || ! -x "${fastboot_bin}" ]]; then
    printf '%s\n' '找不到 fastboot。请先安装 Android Platform Tools，或设置 FASTBOOT_ENHANCE_FASTBOOT。' >&2
    exit 1
fi
if [[ -z "${adb_bin}" || ! -x "${adb_bin}" ]]; then
    printf '%s\n' '找不到 adb。请先安装 Android Platform Tools，或设置 FASTBOOT_ENHANCE_ADB。' >&2
    exit 1
fi

rm -rf "${app_dir}"
mkdir -p "${app_dir}/usr/bin"
install -Dm755 "${build_dir}/FastbootEnhance" "${app_dir}/usr/bin/FastbootEnhance"
install -Dm755 "${fastboot_bin}" "${app_dir}/usr/bin/fastboot"
install -Dm755 "${adb_bin}" "${app_dir}/usr/bin/adb"
install -Dm644 "${project_root}/linux/FastbootEnhance.desktop" "${app_dir}/usr/share/applications/FastbootEnhance.desktop"
install -Dm644 "${project_root}/big_icon.png" "${app_dir}/usr/share/icons/hicolor/256x256/apps/FastbootEnhance.png"
install -Dm644 "${project_root}/LICENSE" "${app_dir}/usr/share/licenses/FastbootEnhance/LICENSE"
if [[ -f "${FASTBOOT_ENHANCE_PLATFORM_TOOLS:-}/NOTICE.txt" ]]; then
    install -Dm644 "${FASTBOOT_ENHANCE_PLATFORM_TOOLS}/NOTICE.txt" \
        "${app_dir}/usr/share/licenses/FastbootEnhance/Android-Platform-Tools-NOTICE.txt"
fi
install -Dm644 "${project_root}/linux/FastbootEnhance.desktop" "${app_dir}/FastbootEnhance.desktop"
install -Dm644 "${project_root}/big_icon.png" "${app_dir}/FastbootEnhance.png"

mkdir -p "${dist_dir}"
rm -f "${dist_dir}/FastbootEnhance-Linux-x86_64.AppImage"
export FASTBOOT_ENHANCE_QT_PLUGINS="${qt_plugins_dir}"
export QMAKE="${project_root}/linux/qmake-portable.sh"
export FASTBOOT_ENHANCE_QMAKE_BIN="${qt_query_bin}"
export NO_STRIP=1
export LINUXDEPLOY_PLUGIN_QT="${qt_plugin}"
"${linuxdeploy}" \
    --appdir "${app_dir}" \
    --executable "${app_dir}/usr/bin/FastbootEnhance" \
    --executable "${app_dir}/usr/bin/fastboot" \
    --executable "${app_dir}/usr/bin/adb" \
    --desktop-file "${app_dir}/usr/share/applications/FastbootEnhance.desktop" \
    --icon-file "${app_dir}/usr/share/icons/hicolor/256x256/apps/FastbootEnhance.png" \
    --plugin qt

extra_qt_plugin_args=()
for plugin in libqwayland.so libqoffscreen.so libqminimal.so; do
    plugin_path="${app_dir}/usr/plugins/platforms/${plugin}"
    if [[ -e "${qt_system_plugins}/platforms/${plugin}" ]]; then
        install -Dm755 "${qt_system_plugins}/platforms/${plugin}" "${plugin_path}"
        extra_qt_plugin_args+=(--deploy-deps-only "${plugin_path}")
    fi
done
for plugin_dir in "${wayland_plugin_dirs[@]}"; do
    mkdir -p "${app_dir}/usr/plugins/${plugin_dir}"
    for plugin in "${qt_plugins_dir}/${plugin_dir}"/*.so; do
        [[ -e "${plugin}" ]] || continue
        plugin_path="${app_dir}/usr/plugins/${plugin_dir}/$(basename "${plugin}")"
        install -Dm755 "${plugin}" "${plugin_path}"
        extra_qt_plugin_args+=(--deploy-deps-only "${plugin_path}")
    done
done

"${linuxdeploy}" \
    --appdir "${app_dir}" \
    --desktop-file "${app_dir}/usr/share/applications/FastbootEnhance.desktop" \
    "${extra_qt_plugin_args[@]}"

appimage_args=()
if [[ -n "${runtime_file}" ]]; then
    if [[ ! -f "${runtime_file}" ]]; then
        printf 'AppImage runtime not found: %s\n' "${runtime_file}" >&2
        exit 1
    fi
    appimage_args+=(--runtime-file "${runtime_file}")
fi

ARCH=x86_64 "${appimagetool}" "${appimage_args[@]}" \
    "${app_dir}" "${dist_dir}/FastbootEnhance-Linux-x86_64.AppImage"
chmod +x "${dist_dir}/FastbootEnhance-Linux-x86_64.AppImage"
printf 'Created %s\n' "${dist_dir}/FastbootEnhance-Linux-x86_64.AppImage"
