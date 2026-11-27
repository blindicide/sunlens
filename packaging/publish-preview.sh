#!/usr/bin/env bash
set -euo pipefail

version=$(<VERSION)
tag="v$version"
[[ $tag == v0.1.1 && ${GITHUB_REF:-} == "refs/tags/$tag" ]]
notes=$(mktemp)
trap 'rm -f -- "$notes"' EXIT
python3 - "$version" > "$notes" <<'PY'
from pathlib import Path
import re
import sys

source = Path("CHANGELOG.md").read_text(encoding="utf-8")
blocks = re.split(r"(?=^## )", source, flags=re.MULTILINE)[1:]
prefix = f"## [{sys.argv[1]}]"
matches = [block.rstrip() for block in blocks if block.splitlines()[0].startswith(prefix)]
if len(matches) != 1:
    raise SystemExit("Release notes missing or ambiguous")
print(matches[0])
PY

deb="sunlens_${version}_amd64.deb"
exe="sunlens-${version}-windows-x64.exe"
[[ -s out/$deb && -s out/$exe ]]
printf '\nQt source: https://download.qt.io/archive/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz\n' >> "$notes"
printf '\nSHA-256:\n\n```text\n' >> "$notes"
(cd out && sha256sum "$deb" "$exe") >> "$notes"
printf '```\n' >> "$notes"

gh release create "$tag" --verify-tag --draft --prerelease --title "SUNLENS $tag" \
    --notes-file "$notes" "out/$deb" "out/$exe"

release_id=$(gh api --paginate "repos/$GITHUB_REPOSITORY/releases" \
    --jq '.[] | select(.tag_name == "v0.1.1" and .draft) | .id')
[[ $release_id =~ ^[0-9]+$ ]]
gh api "repos/$GITHUB_REPOSITORY/releases/$release_id" > "$notes"
python3 - "$notes" "$deb" "$exe" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

release = json.loads(Path(sys.argv[1]).read_text())
assert release["draft"] and release["prerelease"]
actual = {asset["name"]: (asset["size"], asset["digest"]) for asset in release["assets"]}
expected = {}
for name in sys.argv[2:]:
    path = Path("out") / name
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    expected[name] = (path.stat().st_size, "sha256:" + digest)
if actual != expected:
    raise SystemExit("Unexpected release files; leaving the release as draft")
PY
gh release edit "$tag" --draft=false
