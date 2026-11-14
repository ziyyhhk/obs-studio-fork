#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["cryptography>=50.0.2,<51"]
# ///
"""Write the in-app update feed for the ziyyhhk OBS-Studio-Rust fork.

    uv run --script ci/fork_update_feed.py build --tag v<version> --assets <dir> --rsa-key key.pem --out feed

<dir> holds the release's assets plus SHA256SUMS; each asset used is verified
against that checksum first. Outputs:

- manifest.json (+ .sig): the Windows updater's manifest: version, tag, notes
  and per arch (x64, windows-x64.zip / arm64, windows-arm64.zip if present)
  the ZIP's name, url, sha256 and size.
- branches.json (+ .sig): the update branches the app advertises. The app
  adds its built-in stable and nightly branches itself, so this is an empty
  array ("no extra branches to distribute").

.sig files are hex RSA PKCS#1 v1.5 SHA-512 signatures. Keys come from the
private key passed with --rsa-key (kept out of this repo); the public half is
pinned in frontend/data/OBSPublicRSAKey.pem.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
import sys
from pathlib import Path

REPO = "https://github.com/ziyyhhk/obs-studio-fork"
WINDOWS_ARCHES = ("x64", "arm64")


class FeedError(Exception):
    pass


def read_sums(path: Path) -> dict[str, str]:
    sums: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        digest, name = line.split(None, 1)
        sums[name.lstrip("*").strip()] = digest.lower()
    if not sums:
        raise FeedError(f"{path} contains no SHA-256 sums")
    return sums


def checked(assets: Path, sums: dict[str, str], name: str) -> Path:
    path = assets / name
    if not path.is_file():
        raise FeedError(f"{name} is not among the release assets")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if sums.get(name) != digest:
        raise FeedError(f"{name} does not match SHA256SUMS")
    return path


def sign(key_pem: bytes, data: bytes) -> str:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding

    key = serialization.load_pem_private_key(key_pem, password=None)
    return key.sign(data, padding.PKCS1v15(), hashes.SHA512()).hex()


def build(tag: str, assets: Path, key_pem: bytes, out: Path, maybe_tag: str = "v") -> None:
    version_re = re.compile(rf"^{maybe_tag}(\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?)$")
    match = version_re.match(tag)
    if not match:
        raise FeedError(f"{tag!r} is not a {maybe_tag}<version> release tag")
    version = match.group(1)
    sums = read_sums(assets / "SHA256SUMS")

    windows: dict[str, dict] = {}
    for arch in WINDOWS_ARCHES:
        name = f"OBS-Studio-Rust-{version}-windows-{arch}.zip"
        path = checked(assets, sums, name)
        windows[arch] = {
            "name": name,
            "url": f"{REPO}/releases/download/{tag}/{name}",
            "sha256": sums[name],
            "size": path.stat().st_size,
        }
    if not windows:
        raise FeedError("none of the expected Windows ABI assets are present")

    notes_url = f"{REPO}/releases/tag/{tag}"
    notes = f'<p>OBS-Studio-Rust {html.escape(version)} (the ziyyhhk fork build)</p><p><a href="{notes_url}">{notes_url}</a></p>'
    manifest = {
        "version": version,
        "tag": tag,
        "channel": "stable",
        "notes": notes,
        "windows": windows,
    }
    files = {
        "manifest.json": (json.dumps(manifest, indent=2) + "\n").encode(),
        "branches.json": b"[]\n",
    }
    out.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        (out / name).write_bytes(data)
    for name in ("manifest.json", "branches.json"):
        (out / f"{name}.sig").write_text(sign(key_pem, files[name]) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", required=True)
    parser.add_argument("--assets", required=True, type=Path)
    parser.add_argument("--rsa-key", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    try:
        build(args.tag, args.assets, args.rsa_key.read_bytes(), args.out)
    except FeedError as error:
        print(f"::error::{error}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
