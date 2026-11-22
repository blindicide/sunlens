"""Запуск анализатора clang для исходников приложения по compilation database."""

import argparse
import json
import pathlib
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build-directory", required=True)
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
database = json.loads(
    (pathlib.Path(args.build_directory) / "compile_commands.json").read_text(encoding="utf-8")
)
entry = database[0]
compiler = (entry.get("arguments") or shlex.split(entry["command"]))[0]
libgcc = pathlib.Path(
    subprocess.check_output([compiler, "-print-libgcc-file-name"], text=True).strip()
)
extra = []
# Clang не должен выбирать случайную неполную установку GCC другой версии.
if libgcc.is_file():
    extra.append(f"--extra-arg=--gcc-install-dir={libgcc.parent}")
for path in sorted((root / "src").rglob("*.cpp")):
    subprocess.run(
        [
            "clang-tidy",
            str(path),
            "-p",
            args.build_directory,
            "--checks=-*,clang-analyzer-*",
            "--warnings-as-errors=*",
            "--quiet",
            *extra,
        ],
        check=True,
    )
