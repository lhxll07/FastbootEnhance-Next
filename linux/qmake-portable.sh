#!/bin/sh

set -eu

qmake_bin="${FASTBOOT_ENHANCE_QMAKE_BIN:-}"
if [ -z "$qmake_bin" ]; then
    qmake_bin="$(command -v qmake6 2>/dev/null || command -v qmake 2>/dev/null || true)"
fi
if [ -z "$qmake_bin" ]; then
    echo "qmake was not found" >&2
    exit 1
fi

if [ "${1:-}" = "-query" ] && [ -n "${FASTBOOT_ENHANCE_QT_PLUGINS:-}" ]; then
    "$qmake_bin" -query | sed "s#^QT_INSTALL_PLUGINS:.*#QT_INSTALL_PLUGINS:${FASTBOOT_ENHANCE_QT_PLUGINS}#"
    exit 0
fi

exec "$qmake_bin" "$@"
