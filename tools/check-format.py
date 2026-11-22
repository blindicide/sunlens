"""Проверка формата C++ без изменения файлов."""

import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
files = [
    str(path)
    for directory in ("src", "include", "tests")
    for path in (root / directory).rglob("*")
    if path.suffix in (".cpp", ".hpp")
]
if files:
    subprocess.run(["clang-format", "--dry-run", "--Werror", *files], check=True)
