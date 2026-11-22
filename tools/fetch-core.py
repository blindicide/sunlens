"""Загрузка только закреплённого Linux-ядра для неповышенных проверок конфигурации."""

import argparse
import hashlib
import json
import os
import pathlib
import platform
import tarfile
import tempfile
import urllib.request


def acquire(asset, target, opener):
    target.mkdir(parents=True, exist_ok=True, mode=0o700)
    if target.is_symlink():
        raise SystemExit("Каталог загрузки не должен быть символической ссылкой.")
    binary = target / "sing-box"
    if binary.is_symlink():
        raise SystemExit("Путь ядра не должен быть символической ссылкой.")
    if binary.exists():
        if hashlib.sha256(binary.read_bytes()).hexdigest() != asset["binarySha256"]:
            raise SystemExit("Существующее ядро имеет другой SHA-256; перезапись запрещена.")
        return binary.resolve()
    with tempfile.TemporaryDirectory(prefix="fetch-", dir=target) as temporary:
        archive = pathlib.Path(temporary) / "core.tar.gz"
        digest = hashlib.sha256()
        total = 0
        with opener(asset["url"], timeout=30) as response, archive.open("xb") as out:
            if response.status != 200 or not response.url.startswith("https://"):
                raise SystemExit("Неожиданный ответ сервера загрузки.")
            while chunk := response.read(1024 * 1024):
                total += len(chunk)
                if total > 128 * 1024 * 1024:
                    raise SystemExit("Архив ядра превышает допустимый размер.")
                digest.update(chunk)
                out.write(chunk)
        if digest.hexdigest() != asset["archiveSha256"]:
            raise SystemExit("SHA-256 архива не совпадает с manifest.")
        with tarfile.open(archive) as tar:
            member = tar.getmember(asset["binaryMember"])
            if not member.isfile() or member.size > 128 * 1024 * 1024:
                raise SystemExit("Недопустимый исполняемый файл в архиве.")
            content = tar.extractfile(member).read()
        if hashlib.sha256(content).hexdigest() != asset["binarySha256"]:
            raise SystemExit("SHA-256 исполняемого файла не совпадает с manifest.")
        # Извлекается один обычный файл, а не произвольные пути/ссылки из tar.
        staged = pathlib.Path(temporary) / "sing-box"
        staged.write_bytes(content)
        staged.chmod(0o700)
        os.link(staged, binary)
    return binary.resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", required=True, type=pathlib.Path)
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        raise SystemExit("Проверенный артефакт есть только для Linux x86_64.")
    root = pathlib.Path(__file__).resolve().parents[1]
    manifest = json.loads((root / "dependencies.json").read_text(encoding="utf-8"))
    asset = manifest["cores"]["sing-box"]["assets"]["linux-amd64"]
    print(acquire(asset, args.destination, urllib.request.urlopen))


if __name__ == "__main__":
    main()
