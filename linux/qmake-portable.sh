#!/bin/sh

if [ "$1" = "-query" ] && [ -n "${FASTBOOT_ENHANCE_QT_PLUGINS:-}" ]; then
    /usr/bin/qmake6 -query | sed "s#^QT_INSTALL_PLUGINS:.*#QT_INSTALL_PLUGINS:${FASTBOOT_ENHANCE_QT_PLUGINS}#"
    exit 0
fi

exec /usr/bin/qmake6 "$@"
