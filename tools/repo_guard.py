#!/usr/bin/env python3
"""Repository guard for SMAS Mobile.

Fails (exit code 1) when the tracked tree contains anything that must never be
published in this public repository:

  * game files (ROMs) or other copyrighted game data, detected by extension
    AND by content (a Super Nintendo cartridge header anywhere it would sit);
  * save files, save states and backups;
  * signing keys, keystores, passwords, tokens and other credentials;
  * unexpectedly large or unexplained binary files;
  * missing licence / attribution notices.

Usage:
    python3 tools/repo_guard.py            # check every tracked file
    python3 tools/repo_guard.py --staged   # check what is staged for commit
    python3 tools/repo_guard.py --all      # check every file on disk (untracked too)

It needs nothing but Python 3 and git.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- 1. forbidden by name ----------------------------------------------------
ROM_EXT = {
    ".sfc", ".smc", ".swc", ".fig", ".bs", ".st", ".gd3", ".gd7", ".ufo", ".078", ".rom",
    ".nes", ".fds", ".gb", ".gbc", ".gba", ".n64", ".z64", ".v64", ".iso",
    ".bps", ".ips", ".ups", ".xdelta", ".pcm", ".msu",
}
SAVE_EXT = {".srm", ".sav", ".sst", ".state", ".pads", ".smasbackup"}
KEY_EXT = {
    ".jks", ".keystore", ".p12", ".pfx", ".pem", ".key", ".pk8", ".der", ".crt", ".cer",
    ".asc", ".gpg", ".b64",
}
ARCHIVE_EXT = {".zip", ".7z", ".rar", ".tar", ".tgz", ".gz", ".xz", ".bz2", ".apk", ".aab"}
BINARY_EXT = {".so", ".dll", ".dylib", ".exe", ".a", ".o", ".obj", ".class", ".dex"}
FORBIDDEN_NAMES = {
    "keystore.properties", "signing.properties", "secrets.properties", "local.properties",
    ".env", ".netrc", ".git-credentials", "google-services.json",
    "smas_port.ini", "achievements.ini", "rompath.txt",
}
FORBIDDEN_DIRS = {"roms", "rom", "saves", "states", "backups", "msu", "covers"}

# Files that are allowed although a generic rule above would catch them.
ALLOWED = {
    "android/gradle/wrapper/gradle-wrapper.jar",
}

# --- 2. forbidden by content -------------------------------------------------
# (a) Super Nintendo cartridge header: 21-byte title, then map mode, type, ROM
#     size, RAM size, country, licensee, version, and a checksum whose
#     complement precedes it. It sits at $7FC0 (LoROM), $FFC0 (HiROM) or
#     $40FFC0 (ExHiROM), plus 512 when the file has a copier header.
SNES_HEADER_OFFSETS = (0x7FC0, 0xFFC0, 0x40FFC0)
SMAS_TITLES = (b"SUPER MARIO ALL_STARS", b"ALL_STARS + WORLD", b"SUPERMARIO COLLECTION")
# (b) other cartridge / disc images
MAGICS = (
    (0, b"NES\x1a", "iNES cartridge image"),
    (0, b"FDS\x1a", "Famicom Disk System image"),
    (0x104, b"\xce\xed\x66\x66\xcc\x0d\x00\x0b", "Game Boy cartridge image"),
    (0, b"\x80\x37\x12\x40", "Nintendo 64 cartridge image"),
    (0, b"PATCH", "IPS patch"),
    (0, b"BPS1", "BPS patch"),
    (0, b"SMSS", "SMAS Mobile save state"),
    (0, b"\xfe\xed\xfe\xed", "Java keystore (JKS)"),
    (0, b"\xce\xce\xce\xce", "Java keystore (JCEKS)"),
)

SECRET_PATTERNS = [
    (re.compile(rb"-----BEGIN (?:RSA |EC |DSA |OPENSSH |ENCRYPTED |PGP )?PRIVATE KEY"), "private key block"),
    (re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{36,}\b"), "GitHub token"),
    (re.compile(rb"\bgithub_pat_[A-Za-z0-9_]{60,}\b"), "GitHub fine-grained token"),
    (re.compile(rb"\bAKIA[0-9A-Z]{16}\b"), "AWS access key id"),
    (re.compile(rb"\bAIza[0-9A-Za-z_\-]{35}\b"), "Google API key"),
    (re.compile(rb"\bxox[baprs]-[0-9A-Za-z\-]{20,}\b"), "Slack token"),
    # A literal password in a Gradle / properties file. References to
    # environment variables, Gradle properties or CI secrets are fine.
    (re.compile(rb"(?im)^\s*(?:storePassword|keyPassword|signing\.password|RELEASE_STORE_PASSWORD|RELEASE_KEY_PASSWORD)"
                rb"\s*[=:]\s*['\"]?(?!\s*$)(?!.*(?:System\.getenv|\$\{|\$env:|secrets\.|project\.|props|providers\.|"
                rb"findProperty|<|your-|example|changeme|\*\*\*))[^\s'\"#]{4,}"), "literal signing password"),
]

MAX_FILE_BYTES = 1_500_000          # nothing in this repository needs to be bigger
MAX_BINARY_BYTES = 400_000          # ... and binaries (icons, fonts) are far smaller

REQUIRED_FILES = ("LICENSE",)        # extended once the project files are in place (see below)
REQUIRED_WHEN_PRESENT = {
    # if the left path exists, the right ones must too
    "src/core/snes.cpp": (
        "LICENSE", "NOTICE.md", "THIRD_PARTY_NOTICES.md", "docs/UPSTREAM.md",
        "third_party/fonts/LICENSE.txt", "third_party/stb/stb_image.h",
    ),
}
REQUIRED_TEXT = {
    # path -> substrings that must appear (attribution that may not be removed)
    "LICENSE": ("MIT License", "Copyright (c) 2026 Darhkfox"),
}


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", ROOT, *args], check=True, capture_output=True, text=True).stdout


def list_files(mode: str) -> list[str]:
    if mode == "staged":
        out = git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z")
        return [p for p in out.split("\0") if p]
    if mode == "all":
        out = git("ls-files", "-z", "--cached", "--others", "--exclude-standard")
        return [p for p in out.split("\0") if p]
    return [p for p in git("ls-files", "-z").split("\0") if p]


def looks_binary(data: bytes) -> bool:
    if b"\0" in data[:8192]:
        return True
    sample = data[:8192]
    if not sample:
        return False
    text = sum(1 for b in sample if b in (9, 10, 13) or 32 <= b < 127 or b >= 128)
    return text / len(sample) < 0.85


def snes_header_at(data: bytes, off: int) -> str | None:
    """The cartridge title if a plausible SNES header sits at `off`, else None."""
    if off < 0 or off + 0x20 > len(data):
        return None
    h = data[off:off + 0x20]
    title = h[:21]
    if any(b < 0x20 or b > 0x7E for b in title) or not title.strip():
        return None
    checksum = h[0x1E] | (h[0x1F] << 8)
    complement = h[0x1C] | (h[0x1D] << 8)
    if (checksum ^ complement) != 0xFFFF:
        return None
    if (h[0x15] & 0xE0) not in (0x20,):            # map mode byte: 001x xxxx
        return None
    if h[0x17] > 0x0E or h[0x18] > 0x0A:           # ROM / RAM size exponents
        return None
    return title.decode("ascii", "replace").strip()


def check_content(path: str, data: bytes) -> list[str]:
    problems = []
    for off, magic, what in MAGICS:
        if data[off:off + len(magic)] == magic:
            problems.append(f"content is a {what}")
    if len(data) >= 0x8000:
        for copier in (0, 512):
            for base in SNES_HEADER_OFFSETS:
                title = snes_header_at(data, base + copier)
                if title is not None:
                    problems.append(f"content is a Super Nintendo ROM image (cartridge title \"{title}\")")
    if len(data) >= 0x40000 and any(t in data for t in SMAS_TITLES):
        problems.append("large file contains a Super Mario All-Stars cartridge title")
    for rx, what in SECRET_PATTERNS:
        m = rx.search(data)
        if m:
            line = data.count(b"\n", 0, m.start()) + 1
            problems.append(f"contains a {what} (line {line})")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--staged", action="store_true", help="check the files staged for commit")
    g.add_argument("--all", action="store_true", help="check untracked files too")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()
    mode = "staged" if args.staged else "all" if args.all else "tracked"

    files = list_files(mode)
    problems: list[tuple[str, str]] = []
    checked = 0
    for rel in files:
        full = os.path.join(ROOT, rel)
        if not os.path.isfile(full) or os.path.islink(full):
            continue
        checked += 1
        norm = rel.replace("\\", "/")
        if norm in ALLOWED:
            continue
        name = os.path.basename(norm).lower()
        ext = os.path.splitext(name)[1]
        parts = set(p.lower() for p in norm.split("/")[:-1])
        if ext in ROM_EXT:
            problems.append((rel, "game file / game data extension"))
        if ext in SAVE_EXT:
            problems.append((rel, "save file / save state extension"))
        if ext in KEY_EXT or name.startswith(("id_rsa", "id_ed25519")):
            problems.append((rel, "signing key / certificate / credential extension"))
        if ext in ARCHIVE_EXT:
            problems.append((rel, "archive or package (may hide game data or builds)"))
        if ext in BINARY_EXT:
            problems.append((rel, "compiled binary"))
        if name in FORBIDDEN_NAMES or (name.startswith(".env.") and name != ".env.example"):
            problems.append((rel, "local configuration / credentials file"))
        if parts & FORBIDDEN_DIRS:
            problems.append((rel, f"inside a forbidden folder ({', '.join(sorted(parts & FORBIDDEN_DIRS))})"))
        try:
            size = os.path.getsize(full)
            with open(full, "rb") as f:
                data = f.read(MAX_FILE_BYTES * 8)
        except OSError as e:
            problems.append((rel, f"unreadable: {e}"))
            continue
        if size > MAX_FILE_BYTES:
            problems.append((rel, f"too large ({size:,} bytes > {MAX_FILE_BYTES:,})"))
        elif size > MAX_BINARY_BYTES and looks_binary(data):
            problems.append((rel, f"large binary file ({size:,} bytes > {MAX_BINARY_BYTES:,})"))
        # (this script names the patterns it looks for)
        if norm != "tools/repo_guard.py":
            for p in check_content(norm, data):
                problems.append((rel, p))

    if mode != "staged":
        present = set(f.replace("\\", "/") for f in files)
        required = list(REQUIRED_FILES)
        for trigger, needs in REQUIRED_WHEN_PRESENT.items():
            if trigger in present:
                required += needs
        for r in dict.fromkeys(required):
            if r not in present:
                # (before the first licence commit an empty repository is fine)
                if r == "LICENSE" and not any(p.startswith(("src/", "android/")) for p in present):
                    continue
                problems.append((r, "required licence / attribution file is missing"))
        for path, needles in REQUIRED_TEXT.items():
            if path in present:
                with open(os.path.join(ROOT, path), "r", encoding="utf-8", errors="replace") as f:
                    text = f.read()
                for n in needles:
                    if n not in text:
                        problems.append((path, f"required attribution text is missing: \"{n}\""))

    if problems:
        print(f"repo_guard: {len(problems)} problem(s) in {checked} file(s) ({mode}):", file=sys.stderr)
        for rel, why in problems:
            print(f"  ::error file={rel}::{rel}: {why}" if os.environ.get("GITHUB_ACTIONS") else f"  {rel}: {why}",
                  file=sys.stderr)
        print("\nNothing listed above may be committed to this public repository.", file=sys.stderr)
        return 1
    if not args.quiet:
        print(f"repo_guard: OK - {checked} file(s) checked ({mode}): no game data, saves, keys, secrets or stray "
              f"binaries; licence notices present.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
