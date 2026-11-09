#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Writes the list of contributors from contributors.json into the README and into
qml/components/Contributors.js, which the About page shows.

  python3 -I scripts/make-contributors.py                     rewrite both
  python3 -I scripts/make-contributors.py --check             fail if they are out of date, or if
                                                              the list doesn't match the history
  python3 -I scripts/make-contributors.py --check --release   also fail on a "next" left in the list

An entry: the GitHub name, the day of the first commit and the release that person is new in
("next" until that release is made, then its version). Whoever is new in the current version is
highlighted, which ends by itself with the next release.

The history is not read in a shallow clone (CI's), where it doesn't reach the first commits.
"""
import datetime
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "contributors.json")
JS = os.path.join(ROOT, "qml", "components", "Contributors.js")
README = os.path.join(ROOT, "README.md")
BEGIN = "<!-- contributors: written by scripts/make-contributors.py from contributors.json -->"
END = "<!-- /contributors -->"
NEXT = "next"
NOREPLY = re.compile(r"^(?:\d+\+)?([^@]+)@users\.noreply\.github\.com$")


def load():
    with open(DATA, encoding="utf-8") as f:
        entries = json.load(f)
    for entry in entries:
        if set(entry) != {"login", "since", "release"}:
            sys.exit(f"contributors.json: login, since and release in every entry: {entry}")
        datetime.date.fromisoformat(entry["since"])
        if entry["release"] != NEXT and not re.fullmatch(r"\d+\.\d+\.\d+", entry["release"]):
            sys.exit(f"contributors.json: release is a version or \"next\": {entry}")
    if len({entry["login"] for entry in entries}) != len(entries):
        sys.exit("contributors.json: a name is in the list twice")
    return sorted(entries, key=lambda entry: (entry["since"], entry["login"]))


def current_version():
    with open(os.path.join(ROOT, "Cargo.toml"), encoding="utf-8") as f:
        return re.search(r'^\[workspace\.package\].*?^version = "([^"]+)"', f.read(), re.M | re.S).group(1)


def js_text(entries):
    rows = ",\n".join("    " + json.dumps(entry) for entry in entries)
    return ("// SPDX-License-Identifier: AGPL-3.0-only\n"
            "// Written by scripts/make-contributors.py from contributors.json: change that file.\n"
            ".pragma library\n\n"
            f"var list = [\n{rows}\n]\n")


def readme_line(entry, version):
    day = datetime.date.fromisoformat(entry["since"])
    line = (f"- **[{entry['login']}](https://github.com/{entry['login']})**, "
            f"since {day.day} {day.strftime('%B %Y')}")
    if entry["release"] == NEXT:
        return line + ": **new in the next release**"
    if entry["release"] == version:
        return line + f": **new in {version}**"
    return line


def readme_text(entries, version):
    with open(README, encoding="utf-8") as f:
        text = f.read()
    if BEGIN not in text or END not in text:
        sys.exit("README.md has lost its contributors markers")
    head, rest = text.split(BEGIN, 1)
    tail = rest.split(END, 1)[1]
    lines = "\n".join(readme_line(entry, version) for entry in entries)
    return f"{head}{BEGIN}\n{lines}\n{END}{tail}"


def history_problems(entries):
    def git(*args):
        return subprocess.check_output(["git", *args], cwd=ROOT, text=True)

    if git("rev-parse", "--is-shallow-repository").strip() == "true":
        return []
    problems, first = [], {}
    for line in git("log", "--no-merges", "--format=%ae%x00%as%x00%h", "HEAD").splitlines():
        email, date, commit = line.split("\0")
        if "[bot]" in email:
            continue
        match = NOREPLY.match(email)
        if not match:
            problems.append(f"commit {commit}: not a GitHub noreply author, so it matches nobody here")
            continue
        login = match.group(1)
        first[login] = min(first.get(login, date), date)
    listed = {entry["login"]: entry["since"] for entry in entries}
    for login, date in sorted(first.items()):
        if login not in listed:
            problems.append(f"{login} has commits since {date}, but isn't in contributors.json")
        elif listed[login] != date:
            problems.append(f"{login}: contributors.json says {listed[login]}, the history {date}")
    problems += [f"{login} is listed, but has no commits" for login in listed if login not in first]
    return problems


def main():
    check = "--check" in sys.argv[1:]
    release = "--release" in sys.argv[1:]
    entries = load()
    version = current_version()
    js, readme = js_text(entries), readme_text(entries, version)
    if not check:
        with open(JS, "w", encoding="utf-8") as f:
            f.write(js)
        with open(README, "w", encoding="utf-8") as f:
            f.write(readme)
        return
    problems = history_problems(entries)
    for path, wanted in ((JS, js), (README, readme)):
        current = ""
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                current = f.read()
        if current != wanted:
            problems.append(f"{os.path.relpath(path, ROOT)} is out of date: python3 -I scripts/make-contributors.py")
    if release:
        problems += [f"{entry['login']}: replace \"next\" with {version} in contributors.json"
                     for entry in entries if entry["release"] == NEXT]
    for problem in problems:
        print(problem, file=sys.stderr)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
