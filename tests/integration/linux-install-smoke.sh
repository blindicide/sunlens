#!/bin/sh
set -eu

if [ "${SUNLENS_DISPOSABLE_VM:-}" != 1 ] || [ "$(id -u)" = 0 ] ||
    ! systemd-detect-virt --vm --quiet || [ "$(uname -m)" != x86_64 ]; then
    echo 'Требуется отдельная одноразовая Linux x86_64 VM и непривилегированный пользователь.' >&2
    exit 1
fi
if [ "$#" != 1 ]; then
    echo 'Укажите абсолютный путь к DEB или RPM.' >&2
    exit 1
fi
case "$1" in /*.deb|/*.rpm) package=$1 ;; *) exit 1 ;; esac
. /etc/os-release
case "$ID:$VERSION_ID:$package" in
    ubuntu:24.04:*.deb)
        type=DEB
        version=$(dpkg-deb -f "$package" Version)
        expected_name="sunlens-$version-linux-amd64.deb"
        installed() { dpkg-query -W -f='${Status}' sunlens 2>/dev/null | grep -q 'install ok installed'; }
        metadata() { dpkg-query -W -f='${Version}' sunlens; }
        install_package() { sudo -n env DEBIAN_FRONTEND=noninteractive apt-get -y install "$package"; }
        remove_package() { sudo -n env DEBIAN_FRONTEND=noninteractive apt-get -y purge sunlens; }
        ;;
    fedora:44:*.rpm)
        type=RPM
        version=$(rpm -qp --queryformat '%{VERSION}' "$package")
        expected_name="sunlens-$version-linux-x86_64.rpm"
        installed() { rpm -q sunlens >/dev/null 2>&1; }
        metadata() { rpm -q --queryformat '%{VERSION}' sunlens; }
        install_package() { sudo -n dnf -y install "$package"; }
        remove_package() { sudo -n dnf -y remove --no-autoremove sunlens; }
        ;;
    *) echo 'Используйте Ubuntu 24.04/DEB или Fedora 44/RPM.' >&2; exit 1 ;;
esac
if installed; then
    echo 'Пакет уже установлен; нужна чистая VM.' >&2
    exit 1
fi
if command -v qmake6 >/dev/null || command -v cmake >/dev/null || command -v go >/dev/null; then
    echo 'На проверочной VM не должно быть Qt SDK, CMake или Go.' >&2
    exit 1
fi

test "$(basename "$package")" = "$expected_name"
workspace=$(mktemp -d)
trap 'rm -f "$workspace"/before "$workspace"/after "$workspace"/data/sentinel; rmdir "$workspace"/data "$workspace" 2>/dev/null || true' EXIT
export XDG_DATA_HOME="$workspace/data"
export LC_ALL=C.UTF-8
mkdir "$XDG_DATA_HOME"
printf '%s\n' 'sunlens-local-fixture' >"$XDG_DATA_HOME/sentinel"

snapshot() {
    python3 - <<'PY'
import json, subprocess
for family in ("-4", "-6"):
    for kind, extra in (("route", ["table", "all"]), ("rule", [])):
        entries = json.loads(subprocess.check_output(["ip", "-j", family, kind, "show", *extra]))
        for entry in entries:
            for key in ("expires", "used", "lastuse"):
                entry.pop(key, None)
        print(json.dumps(entries, sort_keys=True, indent=2))
PY
}

install_package
installed
test "$(metadata)" = "$version"
test "$(QT_QPA_PLATFORM=offscreen sunlens --version)" = "SUNLENS $version"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_FATAL_WARNINGS=1 sunlens --smoke-test-installed
helper=/usr/libexec/sunlens/sunlens-tunnel-helper
test "$("$helper" --version)" = "SUNLENS $version"
if "$helper" --recover; then
    echo 'Помощник разрешил неавторизованное восстановление.' >&2
    exit 1
fi
sudo -n "$helper" --recover
test -f /usr/share/applications/sunlens.desktop
test -f /usr/share/icons/hicolor/scalable/apps/sunlens.svg
test -f /usr/share/polkit-1/actions/org.sunlens.tunnel.policy
systemctl is-enabled sunlens-recover.service
python3 - "$helper" <<'PY'
import json, selectors, subprocess, sys
helper = sys.argv[1]
with subprocess.Popen(
    ["sudo", "-n", helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE
) as process:
    try:
        with selectors.DefaultSelector() as events:
            events.register(process.stdout, selectors.EVENT_READ)
            if not events.select(10):
                raise SystemExit("Помощник не подтвердил готовность IPC.")
        status = json.loads(process.stdout.readline())
        if status["state"] != "idle":
            raise SystemExit("У установленного помощника нет чистого состояния.")
        attempt = subprocess.run(
            ["sudo", "-n", helper, "--prepare-uninstall"], capture_output=True, text=True
        )
        if attempt.returncode == 0 or "Другой сетевой помощник" not in attempt.stderr:
            raise SystemExit("Действующая helper-сессия не блокирует удаление.")
        print("Установленный helper: IPC и блокировка удаления при активной сессии проверены.")
    finally:
        process.stdin.close()
        if process.wait(timeout=15) != 0:
            raise SystemExit("Не удалось завершить IPC-сессию.")
PY
python3 - <<'PY'
import hashlib, json, pathlib
manifest = json.loads(pathlib.Path("/usr/share/sunlens/dependencies.json").read_text())
for core, entry in manifest["cores"].items():
    file = pathlib.Path("/usr/libexec/sunlens/cores") / core
    with file.open("rb") as data:
        digest = hashlib.file_digest(data, "sha256").hexdigest()
    assert digest == entry["assets"]["linux-amd64"]["binarySha256"], core
    print(core, entry["version"], digest)
PY
snapshot >"$workspace/before"
remove_package
if installed; then
    echo 'Запись пакета осталась после удаления.' >&2
    exit 1
fi
snapshot >"$workspace/after"
diff -u "$workspace/before" "$workspace/after"
test ! -e /usr/bin/sunlens
test ! -e "$helper"
test ! -e /usr/share/applications/sunlens.desktop
test ! -e /usr/share/polkit-1/actions/org.sunlens.tunnel.policy
test ! -e /var/lib/sunlens/recovery.json
test ! -e /run/sunlens/uninstalling
test "$(cat "$XDG_DATA_HOME/sentinel")" = sunlens-local-fixture
printf '%s\n' "$type: зависимости, запуск GUI и удаление проверены; полная TUN-матрица не проверялась."
