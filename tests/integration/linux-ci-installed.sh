#!/bin/sh
set -eu

if [ "${GITHUB_ACTIONS:-}" != true ] || [ "${RUNNER_ENVIRONMENT:-}" != github-hosted ] ||
    [ "$(id -u)" = 0 ] || [ "$#" != 1 ]; then
    echo 'Запускайте проверку только от обычного пользователя на GitHub-hosted runner.' >&2
    exit 1
fi
. /etc/os-release
test "$ID:$VERSION_ID:$(uname -m)" = ubuntu:24.04:x86_64
case "$1" in /*.deb) package=$1 ;; *) exit 1 ;; esac
if dpkg-query -W -f='${Status}' sunlens 2>/dev/null | grep -q 'install ok installed'; then
    echo 'На runner уже установлен SUNLENS.' >&2
    exit 1
fi
test -c /dev/net/tun

helper=/usr/libexec/sunlens/sunlens-tunnel-helper
reports="$PWD/build/evidence/installed-linux"
mkdir -p "$reports"
runtime=$(mktemp -d)
purge() { sudo -n env DEBIAN_FRONTEND=noninteractive apt-get -y purge sunlens; }
cleanup() {
    rmdir "$runtime" 2>/dev/null || true
    if dpkg-query -W sunlens >/dev/null 2>&1; then
        purge
    fi
}
trap cleanup EXIT

sudo -n env DEBIAN_FRONTEND=noninteractive apt-get -y install "$package" tcpdump dbus-daemon
export XDG_RUNTIME_DIR="$runtime"
export LC_ALL=C.UTF-8
test "$(QT_QPA_PLATFORM=offscreen sunlens --version)" = "SUNLENS $(cat VERSION)"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_FATAL_WARNINGS=1 sunlens --smoke-test-installed
systemctl is-enabled --quiet sunlens-recover.service
pkaction --action-id org.sunlens.tunnel | grep -Fx org.sunlens.tunnel
if timeout 10 pkexec --disable-internal-agent "$helper" </dev/null; then
    echo 'Помощник запустился через polkit без подтверждения.' >&2
    exit 1
else
    test "$?" -eq 127
fi

test_installed() {
    core=$1
    shift
    sudo -n env SUNLENS_ISOLATED_CI=1 timeout --kill-after=10 180 \
        unshare --mount --net --fork --kill-child -- \
        /usr/bin/python3 tests/integration/linux-tun.py --installed --report "$reports/$core" "$@"
}
test_installed sing-box
test_installed xray --xray /usr/libexec/sunlens/cores/xray

purge
test ! -e "$helper"
test ! -e /usr/bin/sunlens
test ! -e /usr/share/polkit-1/actions/org.sunlens.tunnel.policy
test ! -e /var/lib/sunlens/recovery.json
test ! -e /run/sunlens/uninstalling
