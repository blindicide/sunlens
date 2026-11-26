#!/bin/sh
set -eu
umask 022

if [ "$#" != 1 ]; then
    echo 'Укажите тип пакета: deb или rpm.' >&2
    exit 1
fi
. /etc/os-release
case "$ID:$VERSION_ID:$(uname -m):$1" in
    ubuntu:24.04:x86_64:deb)
        generator=DEB
        suffix=linux-amd64.deb
        ;;
    fedora:44:x86_64:rpm)
        generator=RPM
        suffix=linux-x86_64.rpm
        ;;
    *)
        echo 'Нужен runner Ubuntu 24.04 x86_64 для DEB или Fedora 44 x86_64 для RPM.' >&2
        exit 1
        ;;
esac

cd "$(dirname "$0")/.."
build="build/package-$1"
cores="$PWD/build/dependencies"
version=$(cat VERSION)
python3 tools/fetch-core.py --core sing-box --destination "$cores"
python3 tools/fetch-core.py --core xray --destination "$cores"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBEXECDIR=libexec \
    "-DSUNLENS_PACKAGE_$generator=ON" \
    "-DSUNLENS_PACKAGE_SING_BOX=$cores/sing-box" \
    "-DSUNLENS_PACKAGE_XRAY=$cores/xray"
cmake --build "$build" --target package --parallel 2
package="$build/sunlens-$version-$suffix"
python3 "tools/verify-$1.py" "$package"
mkdir -p build/package-artifact
cp "$package" build/package-artifact/
cd build/package-artifact
sha256sum "${package##*/}" >"${package##*/}.sha256"
