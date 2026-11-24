"""Проверки метаданных и явно опасных секретов перед сборкой."""

import pathlib
import json
import re
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
version = (root / "VERSION").read_text(encoding="utf-8").strip()
if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", version):
    raise SystemExit("Некорректная версия")
files = subprocess.check_output(
    ["git", "ls-files", "-z"], cwd=root
).decode().split("\0")
patterns = [
    rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----",
    rb"gh[pousr]_[A-Za-z0-9]{30,}",
    rb"AKIA[0-9A-Z]{16}",
]
for name in filter(None, files):
    path = root / name
    if path.is_symlink():
        raise SystemExit(f"Непроверенная символическая ссылка: {name}")
    content = path.read_bytes()
    if any(re.search(pattern, content) for pattern in patterns):
        raise SystemExit(f"Возможный секрет: {name}")
for name in ("LICENSE", "third_party/NOTICE.md", "OVERVIEW.md", "CHANGELOG.md"):
    if not (root / name).is_file():
        raise SystemExit(f"Отсутствует обязательный файл: {name}")
print(f"Метаданные {version}: OK; базовая проверка секретов: OK")
dependencies = json.loads((root / "dependencies.json").read_text(encoding="utf-8"))
qt_version = dependencies["qt"]["ciVersion"]
workflow = (root / ".github/workflows/ci.yml").read_text(encoding="utf-8")
if f"version: '{qt_version}'" not in workflow:
    raise SystemExit("Версия Qt CI расходится с dependency manifest.")
for core in ("sing-box", "xray"):
    asset = dependencies["cores"][core]["assets"]["linux-amd64"]
    for key in ("archiveSha256", "binarySha256"):
        if not re.fullmatch("[0-9a-f]{64}", asset[key]):
            raise SystemExit("Некорректное закрепление SHA-256.")
print("Dependency manifest и версия Qt CI: OK; лицензионный выпуск остаётся заблокирован.")
