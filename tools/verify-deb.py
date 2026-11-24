"""Проверка метаданных, содержимого, прав и SHA-256 ядер пакета DEB."""

import argparse
import hashlib
import json
import pathlib
import subprocess
import tarfile


def verify_deb(package, root):
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if package.name != f"sunlens-{version}-linux-amd64.deb" or package.stat().st_size == 0:
        raise SystemExit("Пакет отсутствует, пуст или имеет неверное имя.")
    for field, expected in (("Package", "sunlens"), ("Architecture", "amd64"), ("Version", version)):
        value = subprocess.check_output(
            ["dpkg-deb", "--field", str(package), field], text=True
        ).strip()
        if value != expected:
            raise SystemExit(f"Неверное поле {field}: {value!r}")
    for script in ("prerm", "postinst", "postrm"):
        subprocess.run(
            ["dpkg-deb", "--info", str(package), script], check=True, stdout=subprocess.DEVNULL
        )
    manifest = json.loads((root / "dependencies.json").read_text(encoding="utf-8"))
    executable_paths = {
        "usr/bin/sunlens": None,
        "usr/libexec/sunlens/sunlens-tunnel-helper": None,
    }
    for core in ("sing-box", "xray"):
        executable_paths[f"usr/libexec/sunlens/cores/{core}"] = (
            manifest["cores"][core]["assets"]["linux-amd64"]["binarySha256"]
        )
    data_paths = {
        "usr/share/applications/sunlens.desktop", "usr/share/icons/hicolor/scalable/apps/sunlens.svg",
        "usr/share/sunlens/LICENSE", "usr/share/sunlens/NOTICE.md",
        "usr/share/sunlens/dependencies.json", "usr/share/sunlens/VERSION",
        "usr/share/polkit-1/actions/org.sunlens.tunnel.policy",
        "usr/lib/systemd/system/sunlens-recover.service",
    }
    seen = set()
    with subprocess.Popen(["dpkg-deb", "--fsys-tarfile", str(package)], stdout=subprocess.PIPE) as p:
        with tarfile.open(mode="r|", fileobj=p.stdout) as archive:
            for member in archive:
                name = member.name.removeprefix("./")
                if not (name in executable_paths or name in data_paths):
                    continue
                if (name in seen or not member.isfile() or member.uid != 0 or member.gid != 0
                        or member.mode & 0o7022 or member.size == 0):
                    raise SystemExit(f"Неверный файл или права: {name}")
                if name in executable_paths:
                    if not member.mode & 0o100:
                        raise SystemExit(f"Нет разрешения на выполнение: {name}")
                    handle = archive.extractfile(member)
                    expected = executable_paths[name]
                    if expected and hashlib.file_digest(handle, "sha256").hexdigest() != expected:
                        raise SystemExit(f"SHA-256 не совпадает: {name}")
                    if not expected and handle.read(4) != b"\x7fELF":
                        raise SystemExit(f"Не ELF: {name}")
                seen.add(name)
        p.stdout.read()
        if p.wait():
            raise SystemExit("Ошибка чтения payload DEB.")
    missing = (executable_paths.keys() | data_paths) - seen
    if missing:
        raise SystemExit(f"Не хватает файлов: {', '.join(sorted(missing))}")
    with package.open("rb") as handle:
        print(f"{hashlib.file_digest(handle, 'sha256').hexdigest()}  {package.name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=pathlib.Path)
    args = parser.parse_args()
    verify_deb(args.package.resolve(), pathlib.Path(__file__).resolve().parents[1])
