#!/usr/bin/env python3
"""
scripts/bump.py — DJI Tello CLI version bump (port of panorama-ai/scripts/bump.py).

Usage:
    python3 scripts/bump.py patch            # 0.0.1 → 0.0.2
    python3 scripts/bump.py minor            # 0.0.1 → 0.1.0
    python3 scripts/bump.py major            # 0.0.1 → 1.0.0
    python3 scripts/bump.py patch --dry-run  # preview only, nothing written

Steps:
  1. Read MAJOR.MINOR.PATCH from Makefile.version (the single source of truth).
  2. Write the incremented version back.
  3. Add a CHANGELOG.md section from the git log since the previous bump commit.
  4. Stamp "✅ Выполнена" tasks in TODO.md with "(vX.Y.Z, YYYY-MM-DD)", then delete
     stamped and closed task blocks.
  5. Regenerate the board table in TODO.md.
  6. git commit "chore: bump version to vX.Y.Z".

Commit classification (AGENTS.md → COMMIT CONVENTION):
  [TEL-XXXXX] BF module: description → ### Fixed
  [TEL-XXXXX] module: description    → ### Added
  docs(...): / chore(...): and anything without a TEL number → skipped
"""

import argparse
import re
import subprocess
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_FILE = ROOT / "Makefile.version"
CHANGELOG = ROOT / "CHANGELOG.md"
TODO = ROOT / "TODO.md"
TASK_LIMIT = 30

_VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")


# ── version ──────────────────────────────────────────────────────────────


def read_version() -> tuple[int, int, int]:
    text = VERSION_FILE.read_text().strip()
    m = _VERSION_RE.match(text)
    if not m:
        raise SystemExit(f"Makefile.version: expected MAJOR.MINOR.PATCH, got '{text}'")
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def next_version(ver: tuple[int, int, int], kind: str) -> tuple[int, int, int]:
    major, minor, patch = ver
    if kind == "major":
        return major + 1, 0, 0
    if kind == "minor":
        return major, minor + 1, 0
    return major, minor, patch + 1


def fmt(ver: tuple[int, int, int]) -> str:
    return ".".join(map(str, ver))


# ── git log ──────────────────────────────────────────────────────────────

_BUMP_RE = re.compile(r"^chore: bump version to v\d+\.\d+\.\d+$")


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(ROOT), *args],
                          capture_output=True, text=True, check=True).stdout


def commits_since_last_bump() -> list[str]:
    """Commit subjects after the previous bump commit (all history if there is none)."""
    last_bump = None
    for line in git("log", "--format=%H\t%s").splitlines():
        hash_, _, subject = line.partition("\t")
        if _BUMP_RE.match(subject):
            last_bump = hash_
            break
    rev = f"{last_bump}..HEAD" if last_bump else "HEAD"
    return [s for s in git("log", rev, "--format=%s").splitlines() if s.strip()]


# ── CHANGELOG ────────────────────────────────────────────────────────────

_TEL_COMMIT_RE = re.compile(r"^((?:\[TEL-\d{5}(?:-\d{2})?\])+)\s+(BF\s+)?(.+)$")
_TEL_ID_RE = re.compile(r"TEL-\d{5}(?:-\d{2})?")


def classify(subject: str) -> tuple[str, list[str], str]:
    """(kind, task ids, description); kind is 'fixed', 'added' or 'skip'."""
    m = _TEL_COMMIT_RE.match(subject.strip())
    if not m:
        return "skip", [], subject
    return ("fixed" if m.group(2) else "added"), _TEL_ID_RE.findall(m.group(1)), m.group(3)


def build_section(version: str, commits: list[str]) -> str:
    fixed: list[str] = []
    added: list[str] = []
    for subject in commits:
        kind, ids, desc = classify(subject)
        if kind == "skip":
            continue
        tags = "".join(f"**[{i}]** " for i in ids)
        (fixed if kind == "fixed" else added).append(f"- {tags}{desc}")

    lines = [f"## [{version}] — {date.today().isoformat()}", ""]
    if fixed:
        lines += ["### Fixed", *fixed, ""]
    if added:
        lines += ["### Added", *added, ""]
    if not fixed and not added:
        lines += ["_(no user-facing changes)_", ""]
    lines += ["---", ""]
    return "\n".join(lines)


CHANGELOG_HEADER = """# Changelog

All notable changes to DJI Tello CLI are documented here.
Format: `## [version] — YYYY-MM-DD` followed by grouped changes. Written by `make bump-version`.

---
"""


def update_changelog(version: str, commits: list[str]) -> None:
    text = CHANGELOG.read_text() if CHANGELOG.exists() else CHANGELOG_HEADER
    section = build_section(version, commits)
    marker = "---\n"
    pos = text.find(marker)
    if pos == -1:
        text = text.rstrip() + "\n\n" + section
    else:
        pos += len(marker)
        text = text[:pos] + "\n" + section + text[pos:]
    CHANGELOG.write_text(text)


# ── TODO.md ──────────────────────────────────────────────────────────────

_DONE_RE = re.compile(r"(- \*\*Status:\*\* ✅ Выполнена)(?!\s*\(v)")
_STAMPED_RE = re.compile(r"✅ Выполнена \(v\d+\.\d+\.\d+")
_CLOSED_RE = re.compile(r"^- \*\*Status:\*\*\s*Закрыта", re.MULTILINE)
_TASK_HEADER_RE = re.compile(r"^### \[TEL-\d+(?:-\d{2})?\]")
# A task block ends at the next "###" (task or group heading such as "### 🛰️ Core") or "##".
_BLOCK_END_RE = re.compile(r"^##")


def stamp(text: str, version: str) -> tuple[str, int]:
    count = len(_DONE_RE.findall(text))
    return _DONE_RE.sub(rf"\1 (v{version}, {date.today().isoformat()})", text), count


def split_blocks(text: str) -> list[tuple[bool, list[str]]]:
    """Split into (is_task_block, lines) chunks; task blocks run up to the next heading."""
    chunks: list[tuple[bool, list[str]]] = []
    for line in text.split("\n"):
        if _TASK_HEADER_RE.match(line):
            chunks.append((True, [line]))
        elif _BLOCK_END_RE.match(line) or not chunks or not chunks[-1][0]:
            if chunks and not chunks[-1][0]:
                chunks[-1][1].append(line)
            else:
                chunks.append((False, [line]))
        else:
            chunks[-1][1].append(line)
    return chunks


def is_purgeable(block: list[str]) -> bool:
    body = "\n".join(block)
    return bool(_STAMPED_RE.search(body) or _CLOSED_RE.search(body))


def purge(text: str) -> tuple[str, int]:
    out: list[str] = []
    deleted = 0
    for is_task, lines in split_blocks(text):
        if is_task and is_purgeable(lines):
            deleted += 1
            continue
        out.extend(lines)
    # Collapse the blank runs left by deleted blocks.
    final: list[str] = []
    for line in out:
        if line.strip() == "" and final and final[-1].strip() == "":
            continue
        final.append(line)
    return "\n".join(final).rstrip("\n") + "\n", deleted


_STATUS_LABELS = [
    ("✅", "✅ Выполнена"),
    ("❗", "❗ Владелец"),
    ("Заблокирована", "🔒 Заблокирована"),
    ("В работе", "🔄 В работе"),
    ("Идея", "💡 Идея"),
    ("Открыта", "📂 Открыта"),
]


def board_label(status: str) -> str:
    for needle, label in _STATUS_LABELS:
        if needle in status:
            return label
    return status[:20]


def regen_board(text: str) -> tuple[str, int]:
    tasks: list[dict] = []
    for is_task, lines in split_blocks(text):
        if not is_task:
            continue
        m = re.match(r"^### \[(TEL-[\d-]+)\]\s+(.*)", lines[0])
        task = {"id": m.group(1), "title": m.group(2), "status": "", "complexity": ""}
        for line in lines[1:]:
            if line.startswith("- **Status:**"):
                task["status"] = line.removeprefix("- **Status:**").strip()
            elif line.startswith("- **Complexity:**"):
                task["complexity"] = line.removeprefix("- **Complexity:**").strip()
        if not task["status"].startswith("Закрыта"):
            tasks.append(task)
    tasks.sort(key=lambda t: t["id"])

    open_count = sum(1 for t in tasks if "✅" not in t["status"])
    warning = ""
    if open_count > TASK_LIMIT:
        warning = (f"\n> ⚠️ **ГОРШОЧЕК НЕ ВАРИ!** Открытых задач: **{open_count}/{TASK_LIMIT}**.\n")
    rows = ["| ID | Задача | Статус | Сл. |", "|---|---|---|---|"]
    rows += [f"| {t['id']} | {t['title']} | {board_label(t['status'])} | {t['complexity']} |"
             for t in tasks]
    board = f"## 📋 Доска задач{warning}\n### 🏃 Текущий спринт\n\n" + "\n".join(rows) + "\n\n"

    board_re = re.compile(r"## 📋 Доска задач.*?(?=## 📝 Детали задач)", re.DOTALL)
    if not board_re.search(text):
        raise SystemExit("TODO.md: board section '## 📋 Доска задач' not found")
    return board_re.sub(lambda _: board, text, count=1), open_count


# ── main ─────────────────────────────────────────────────────────────────


def main() -> None:
    parser = argparse.ArgumentParser(description="Bump Makefile.version, CHANGELOG.md, TODO.md")
    parser.add_argument("kind", choices=["patch", "minor", "major"])
    parser.add_argument("--dry-run", action="store_true", help="preview, write nothing")
    args = parser.parse_args()

    old = read_version()
    new = fmt(next_version(old, args.kind))
    print(f"Bump: {fmt(old)} → {new}  ({args.kind})")

    commits = commits_since_last_bump()
    print(f"Commits since last bump: {len(commits)}")
    for c in commits:
        print(f"  {c}")

    todo, stamped = stamp(TODO.read_text(), new)
    todo, purged = purge(todo)
    todo, open_count = regen_board(todo)

    if args.dry_run:
        print("\n─── CHANGELOG preview ───────────────────────────────")
        print(build_section(new, commits))
        print(f"TODO.md: would stamp {stamped} task(s) with v{new}")
        print(f"TODO.md: would delete {purged} completed/closed task block(s), {open_count} open left")
        print("─── dry-run complete (nothing written) ──────────────")
        return

    if git("status", "--porcelain", "--", "Makefile.version", "CHANGELOG.md", "TODO.md").strip():
        sys.exit("Makefile.version, CHANGELOG.md or TODO.md has uncommitted changes: commit them first")

    VERSION_FILE.write_text(new + "\n")
    update_changelog(new, commits)
    TODO.write_text(todo)
    print(f"✓ Makefile.version → {new}")
    print("✓ CHANGELOG.md updated")
    print(f"✓ TODO.md: stamped {stamped}, deleted {purged}, {open_count} open")

    git("add", "--", "Makefile.version", "CHANGELOG.md", "TODO.md")
    git("commit", "-q", "-m", f"chore: bump version to v{new}")
    print(f"✓ git commit → chore: bump version to v{new}")


if __name__ == "__main__":
    main()
