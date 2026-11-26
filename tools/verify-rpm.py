"""Проверка метаданных, имён, владельцев и SHA-256 ядер пакета RPM."""

import argparse
import hashlib
import json
import pathlib
import subprocess


def query(package, format_string):
    return subprocess.check_output(
        ["rpm", "-qp", "--queryformat", format_string, str(package)], text=True
    )


def verify_rpm(package, root):
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if package.name != f"sunlens-{version}-linux-x86_64.rpm" or package.stat().st_size == 0:
        raise SystemExit("Пакет отсутствует, пуст или имеет неверное имя.")
    subprocess.run(["rpm", "-K", "--nosignature", str(package)], check=True)
    if query(package, "%{NAME}\n%{VERSION}\n%{ARCH}\n").splitlines() != ["sunlens", version, "x86_64"]:
        raise SystemExit("Метаданные RPM не совпадают с VERSION.")
    scripts = subprocess.check_output(["rpm", "-qp", "--scripts", str(package)], text=True)
    if "--prepare-uninstall" not in scripts or "--finish-install" not in scripts:
        raise SystemExit("В RPM отсутствуют сценарии обслуживания помощника.")
    manifest = json.loads((root / "dependencies.json").read_text(encoding="utf-8"))
    entries = {}
    output = query(package, "[%{FILENAMES}\t%{FILEMODES:octal}\t%{FILEUSERNAME}\t"
                            "%{FILEGROUPNAME}\t%{FILEDIGESTS}\n]")
    for line in output.splitlines():
        path, mode, user, group, digest = line.split("\t")
        if path in entries:
            raise SystemExit(f"Повторяющийся путь: {path}")
        entries[path] = (int(mode, 8), user, group, digest)
    required = {
        "/usr/bin/sunlens": True,
        "/usr/libexec/sunlens/sunlens-tunnel-helper": True,
        "/usr/share/applications/sunlens.desktop": False,
        "/usr/share/icons/hicolor/scalable/apps/sunlens.svg": False,
        "/usr/share/sunlens/VERSION": False,
        "/usr/share/sunlens/dependencies.json": False,
        "/usr/share/sunlens/LICENSE": False,
        "/usr/share/sunlens/NOTICE.md": False,
        "/usr/share/polkit-1/actions/org.sunlens.tunnel.policy": False,
        "/usr/lib/systemd/system/sunlens-recover.service": False,
    }
    for core in ("sing-box", "xray"):
        expected = manifest["cores"][core]["assets"]["linux-amd64"]["binarySha256"]
        path = f"/usr/libexec/sunlens/cores/{core}"
        required[path] = True
        if path not in entries or entries[path][3] != expected:
            raise SystemExit(f"SHA-256 не совпадает: {path}")
    for path, executable in required.items():
        if path not in entries:
            raise SystemExit(f"Не хватает файла: {path}")
        mode, user, group, digest = entries[path]
        if (mode & 0o170000 != 0o100000 or mode & 0o7022 or user != "root" or group != "root"
                or (executable and not mode & 0o100) or not digest):
            raise SystemExit(f"Неверные права или содержимое: {path}")
    for path in ("/usr/libexec/sunlens", "/usr/libexec/sunlens/cores"):
        if path not in entries:
            raise SystemExit(f"Не хватает каталога: {path}")
        mode, user, group, _ = entries[path]
        if mode != 0o040755 or user != "root" or group != "root":
            raise SystemExit(f"Небезопасные права каталога: {path}")
    with package.open("rb") as handle:
        print(f"{hashlib.file_digest(handle, 'sha256').hexdigest()}  {package.name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=pathlib.Path)
    args = parser.parse_args()
    verify_rpm(args.package.resolve(), pathlib.Path(__file__).resolve().parents[1])
