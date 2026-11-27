#!/usr/bin/env bash
# Run on Ubuntu 24.04 amd64 after building Sunlens against the distro Qt.
set -euo pipefail
umask 022

if [[ $# != 2 ]] || [[ $(dpkg --print-architecture) != amd64 ]]; then
    printf 'Usage on amd64: bash %s <release-build-dir> <output-dir>\n' "$0" >&2
    exit 2
fi

source_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
version=$(<"$source_dir/cmake/VERSION")
[[ $version =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]
mkdir -p -- "$2"
output="$2/sunlens_${version}_amd64.deb"
if [[ -e $output ]]; then
    printf 'Refusing to overwrite %s\n' "$output" >&2
    exit 1
fi

stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
cmake --install "$1" --config Release --prefix "$stage/usr" --strip
mkdir -p "$stage/DEBIAN" "$stage/usr/share/doc/sunlens"
cp "$source_dir/LICENSE" "$stage/usr/share/doc/sunlens/copyright"
find "$stage" -type d -exec chmod 0755 {} +

cat > "$stage/DEBIAN/control" <<EOF
Package: sunlens
Version: $version
Section: net
Priority: optional
Architecture: amd64
Maintainer: Sunlens <216763350+blindicide@users.noreply.github.com>
Homepage: https://github.com/blindicide/sunlens
Depends: ca-certificates, libc6 (>= 2.38), libgcc-s1, libstdc++6 (>= 13.1), libqt6core6t64 (>= 6.4), libqt6gui6t64, libqt6network6t64, libqt6qml6, libqt6quick6, libqt6quickcontrols2-6, libqt6svg6, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-dialogs, qml6-module-qtquick-layouts, qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qtqml-models, qml6-module-qtqml-workerscript
Description: Sunlens desktop proxy preview
 Tests HTTP CONNECT and SOCKS5 proxies and exports configuration for manual
 Windows TUN testing. Does not change Linux system routing.
EOF

dpkg-deb --root-owner-group --threads-max=1 -Zxz -z3 --build "$stage" "$output"
