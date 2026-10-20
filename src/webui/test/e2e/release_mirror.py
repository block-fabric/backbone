#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A local mirror of the GitHub releases of a sidechain, for the end-to-end tests of the downloads
(webui/moduleinstaller.h, doc/unified-app.md "Downloads and updates").

  release_mirror.py make --stem layers --bindir <dir with layersd, layers-cli> --tag T [--tag T2 ...]
                         [--old-release-dir <dir> --old-tag 20261020.1] [--key-hex <privkey>] --out <dir>
                         [--base-url https://127.0.0.1:<port>]
  release_mirror.py serve --dir <dir> --port <p> --cert <pem> --key <pem>     (prints "ready <port>")

The layout is what the app asks (ModuleInstaller, with -sidechainreleaseapi=<base>):
  <out>/repos/block-fabric/<stem>/releases                        the list (GitHub API JSON, newest first)
  <out>/block-fabric/<stem>/releases/download/<tag>/<file>        the files of each release
Each --tag is a release the app can use: the package laid out as the real ones, SHA256SUMS-<stem>.txt,
manifest-<stem>.json (hostApi 1) and manifest-<stem>.json.sig (BIP340, tagged hash "ChainsReleaseManifest/1",
by TEST_PRIVKEY_HEX unless --key-hex). The old release (a real one, e.g. 20261020.1) has no manifest: the app
must refuse it. Importable: make(), make_cert(), serve(), TEST_PRIVKEY_HEX, TEST_PUBKEY_HEX.
"""
import argparse
import hashlib
import http.server
import json
import os
import shutil
import ssl
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "test" / "functional"))
from test_framework.key import compute_xonly_pubkey, sign_schnorr, TaggedHash  # noqa: E402

OWNER = "block-fabric"
PLATFORM = "x86_64-linux-gnu"
HOST_API = 1
SIGNATURE_TAG = "ChainsReleaseManifest/1"
# The key of the tests (never a release key): its x-only public key is given to the app with -sidechainreleasekey.
TEST_PRIVKEY_HEX = hashlib.sha256(b"sidechain modules e2e test release key").hexdigest()
TEST_PUBKEY_HEX = compute_xonly_pubkey(bytes.fromhex(TEST_PRIVKEY_HEX))[0].hex()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sign_manifest(data: bytes, key_hex: str) -> str:
    msg = TaggedHash(SIGNATURE_TAG, data)
    return sign_schnorr(bytes.fromhex(key_hex), msg, aux=bytes(32)).hex()


def _package(stem: str, bindir: Path, tag: str, dest: Path, strip: bool) -> Path:
    top = f"{stem}-{tag}-{PLATFORM}"
    name = f"{top}.tar.gz"
    with tempfile.TemporaryDirectory() as work:
        pkg = Path(work) / top
        pkg.mkdir()
        for b in (f"{stem}d", f"{stem}-cli"):
            shutil.copy2(bindir / b, pkg / b)
            if strip and shutil.which("strip"):
                subprocess.run(["strip", "--strip-debug", str(pkg / b)], check=False)
            os.chmod(pkg / b, 0o755)
        (pkg / "README.txt").write_text(f"{stem} {tag} (a release of the tests' mirror)\n")
        with tarfile.open(dest / name, "w:gz") as tar:
            tar.add(pkg, arcname=top)
    return dest / name


def _asset(base_url: str, stem: str, tag: str, path: Path) -> dict:
    return {"name": path.name, "size": path.stat().st_size,
            "browser_download_url": f"{base_url}/{OWNER}/{stem}/releases/download/{tag}/{path.name}"}


def make(stem: str, bindir: Path, tags: list, old_release: Path | None, old_tag: str, key_hex: str, out: Path,
         base_url: str = "https://127.0.0.1", strip: bool = True) -> Path:
    """Build the mirror in `out`; returns it. Each of `tags` a compatible signed release from `bindir`."""
    out = Path(out)
    releases = []
    if old_release is not None:
        d = out / OWNER / stem / "releases" / "download" / old_tag
        d.mkdir(parents=True, exist_ok=True)
        assets = []
        for name in (f"{stem}-{old_tag}-{PLATFORM}.tar.gz", f"SHA256SUMS-{stem}.txt"):
            src = Path(old_release) / name
            if src.exists():
                shutil.copy2(src, d / name)
                assets.append(_asset(base_url, stem, old_tag, d / name))
        releases.append({"tag_name": old_tag, "name": f"{stem} {old_tag}", "draft": False, "prerelease": True,
                         "published_at": "2026-10-20T01:59:13Z", "assets": assets})
    for i, tag in enumerate(sorted(tags)):
        d = out / OWNER / stem / "releases" / "download" / tag
        d.mkdir(parents=True, exist_ok=True)
        archive = _package(stem, Path(bindir), tag, d, strip)
        sha = sha256_file(archive)
        (d / f"SHA256SUMS-{stem}.txt").write_text(f"{sha}  {archive.name}\n")
        manifest = {"format": 1, "stem": stem, "version": tag, "hostApi": HOST_API,
                    "platforms": {PLATFORM: {"file": archive.name, "sha256": sha, "size": archive.stat().st_size,
                                             "binaries": [f"{stem}d", f"{stem}-cli"]}}}
        data = (json.dumps(manifest, indent=2) + "\n").encode()
        (d / f"manifest-{stem}.json").write_bytes(data)
        (d / f"manifest-{stem}.json.sig").write_text(sign_manifest(data, key_hex) + "\n")
        assets = [_asset(base_url, stem, tag, d / n) for n in
                  (archive.name, f"SHA256SUMS-{stem}.txt", f"manifest-{stem}.json", f"manifest-{stem}.json.sig")]
        releases.append({"tag_name": tag, "name": f"{stem} {tag}", "draft": False, "prerelease": True,
                         "published_at": f"2026-10-2{1 + i}T00:00:00Z", "assets": assets})
    releases.reverse()  # newest first, as GitHub lists them
    api = out / "repos" / OWNER / stem / "releases"
    api.parent.mkdir(parents=True, exist_ok=True)
    api.write_text(json.dumps(releases, indent=1))
    return out


def make_cert(cert: Path, key: Path) -> None:
    """A self-signed certificate for 127.0.0.1 (the app trusts it with -sidechaindownloadcacert)."""
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2", "-subj", "/CN=127.0.0.1",
                    "-addext", "subjectAltName=IP:127.0.0.1", "-keyout", str(key), "-out", str(cert)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


class _Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve(directory: Path, port: int, cert: Path, key: Path) -> None:
    if not Path(cert).exists() or not Path(key).exists():
        make_cert(Path(cert), Path(key))

    def handler(*a, **kw):
        return _Quiet(*a, directory=str(directory), **kw)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(str(cert), str(key))
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    print(f"ready {server.server_address[1]}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make")
    m.add_argument("--stem", default="layers")
    m.add_argument("--bindir", required=True, type=Path)
    m.add_argument("--tag", action="append", required=True)
    m.add_argument("--tag2", action="append", default=[])
    m.add_argument("--old-release-dir", type=Path)
    m.add_argument("--old-tag", default="20261020.1")
    m.add_argument("--key-hex", default=TEST_PRIVKEY_HEX)
    m.add_argument("--out", required=True, type=Path)
    m.add_argument("--base-url", default="https://127.0.0.1")
    m.add_argument("--no-strip", action="store_true")
    s = sub.add_parser("serve")
    s.add_argument("--dir", required=True, type=Path)
    s.add_argument("--port", required=True, type=int)
    s.add_argument("--cert", required=True, type=Path)
    s.add_argument("--key", required=True, type=Path)
    a = p.parse_args()
    if a.cmd == "make":
        make(a.stem, a.bindir, a.tag + a.tag2, a.old_release_dir, a.old_tag, a.key_hex, a.out, a.base_url, not a.no_strip)
        print(f"mirror in {a.out}; key {TEST_PUBKEY_HEX if a.key_hex == TEST_PRIVKEY_HEX else '(given)'}")
    else:
        serve(a.dir, a.port, a.cert, a.key)


if __name__ == "__main__":
    main()
