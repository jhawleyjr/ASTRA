#!/usr/bin/env python3
"""no-main.sh rule 3: refuse a `git commit` whose message would carry a co-author trailer.

Reads the PreToolUse JSON on stdin (never through the environment, which caps one string at
128 KiB). Prints nothing and exits 0 when the command is clean; prints the reason and exits 0 when
it refuses; any exit other than 0 is a scanner failure, which no-main.sh also refuses (fail closed).

The message is checked where it can arrive: the command text (after removing `\\`-newline
continuations and decoding `$'...'`), so a heredoc or an `-m` value with real or escaped newlines
is seen; each `-m`/`--message` and `--trailer` value, found by tokenizing (prose inside a message
is a value, not an option); and each `-F`/`--file` file, resolved against `git -C <dir>` when
given, else the hook's cwd. `-F -` is checked only when stdin is a heredoc in the text or a
literal `< file`; any other stdin, any file or message behind an expansion, and any expansion in
the body of a heredoc with an unquoted delimiter, is refused as unverifiable. Commands are split
at unquoted newlines as well as `;`, `&&`, `|`, and every `git ... commit` in them is checked.

Gaps: `-C`/`-c`/`--amend --no-edit` reuse a message; an inline `cd` before the commit is not
followed (a relative -F path is looked up from cwd, usually refused as missing).
"""

from __future__ import annotations

import codecs
import json
import os
import re
import shlex
import sys

TRAILER_LINE = re.compile(r"(?im)^[\s\"'$]*co-authored-by\s*:")
TRAILER_KEY = re.compile(r"(?i)^[\s\"'$]*co-authored-by\b")
# A heredoc operator: group 1 is a quote or backslash on the delimiter (the body is then literal),
# group 3 the delimiter word.
HEREDOC = re.compile(r"<<-?[ \t]*(\\|(['\"]))?([A-Za-z_][A-Za-z0-9_]*)\2?")
ANSI_C = re.compile(r"\$'((?:[^'\\]|\\.)*)'", re.DOTALL)
# An expansion whose value the hook cannot see. `$(cat <<...` is allowed: its heredoc body is in
# the command text, which is scanned whole.
EXPANSION = re.compile(r"`|\$\((?!\s*cat\s+<<)|\$\{|\$[A-Za-z_]")
# Statement separators, newline included (an unquoted newline ends a command, as `;` does).
SEPARATOR_CHARS = set(";&|()\n")
GIT_VALUE_OPTS = {"-C", "-c", "--git-dir", "--work-tree", "--namespace", "--exec-path"}
LONG_VALUE = {
    "--template",
    "--reuse-message",
    "--reedit-message",
    "--author",
    "--date",
    "--cleanup",
    "--fixup",
    "--squash",
    "--pathspec-from-file",
}
SHORT_VALUE = set("Cct")
SHORT_ATTACHED = set("uS")


class RefusalError(Exception):
    pass


# A `$` or backtick the shell keeps literal, kept apart from real expansions after tokenizing
LITERAL = {"$": "\x00", "`": "\x01"}


def decode_ansi_c(text: str) -> str:
    def repl(m: re.Match[str]) -> str:
        try:
            body = codecs.decode(m.group(1), "unicode_escape")
        except UnicodeDecodeError:
            body = m.group(1)
        return shlex.quote(body)

    return ANSI_C.sub(repl, text)


def protect_literals(text: str) -> str:
    """Replace each `$` and backtick that the shell keeps literal (inside single quotes, or
    escaped) so a value like -m 'uses $ROOT and `x`' is not mistaken for an expansion after
    tokenizing drops the quotes."""
    out: list[str] = []
    state = ""
    i = 0
    while i < len(text):
        c = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "'":
            out.append(LITERAL.get(c, c))
            state = "" if c == "'" else state
        elif c == "\\" and nxt and (state == "" or nxt in '$`"\\'):
            out.append(c + LITERAL.get(nxt, nxt))
            i += 1
        else:
            out.append(c)
            if c in "'\"" and state == "":
                state = c
            elif c == '"' and state == '"':
                state = ""
        i += 1
    return "".join(out)


def strip_heredoc_bodies(text: str) -> str:
    """The command text without heredoc bodies: they hold free text, apostrophes included, that a
    tokenizer cannot parse, and the whole-text scan has already checked them. The body of a heredoc
    whose delimiter is unquoted is expanded by the shell, so an expansion in it is refused."""
    lines = text.split("\n")
    out: list[str] = []
    pending: list[tuple[str, bool, bool]] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        out.append(line)
        for m in HEREDOC.finditer(line):
            if line[m.start() :].startswith("<<<"):
                continue
            dash = line[m.start() :].startswith("<<-")
            pending.append((m.group(3), dash, m.group(1) is None))
        i += 1
        while pending:
            delim, dash, expands = pending.pop(0)
            body: list[str] = []
            while i < len(lines):
                body_line = lines[i].lstrip("\t") if dash else lines[i]
                i += 1
                if body_line == delim:
                    break
                body.append(body_line)
            unescaped = re.sub(r"\\[$`\\]", "", "\n".join(body))
            if expands and EXPANSION.search(unescaped):
                raise RefusalError(
                    f"cannot verify the <<{delim} heredoc: its delimiter is unquoted and its body "
                    f"holds a shell expansion; quote the delimiter (<<'{delim}')."
                )
    return "\n".join(out)


def tokens(text: str) -> list[str]:
    lex = shlex.shlex(text, posix=True, punctuation_chars=";&|()<>\n")
    lex.whitespace = " \t\r"
    lex.whitespace_split = True
    try:
        return list(lex)
    except ValueError as e:
        raise RefusalError(
            f"could not parse the commit command ({e}); use -m or a quoted heredoc."
        ) from None


def statements(toks: list[str]) -> list[list[str]]:
    out: list[list[str]] = [[]]
    for t in toks:
        if set(t) <= SEPARATOR_CHARS:
            out.append([])
        else:
            out[-1].append(t)
    return [s for s in out if s]


def check_message(value: str, what: str) -> None:
    if TRAILER_LINE.search(value):
        raise RefusalError(f"the {what} carries a Co-Authored-By trailer.")
    if EXPANSION.search(value):
        raise RefusalError(f"cannot verify the {what} '{value[:60]}': it holds a shell expansion.")


def read_message_file(path: str, base: str | None, what: str) -> None:
    if not path or EXPANSION.search(path):
        raise RefusalError(f"cannot verify the {what} '{path}' (no literal path).")
    path = os.path.expanduser(path)
    if not os.path.isabs(path):
        if base is None:
            raise RefusalError(
                f"cannot resolve the {what} '{path}' against a git -C directory that is "
                "not a literal path."
            )
        path = os.path.join(base, path)
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        raise RefusalError(
            f"cannot read the {what} '{path}' to check it; write it in an earlier command."
        ) from None
    if TRAILER_LINE.search(text):
        raise RefusalError(f"the {what} '{path}' carries a Co-Authored-By trailer.")


def check_commit(args: list[str], base: str | None, cwd: str) -> None:
    files: list[str] = []
    stdin_files: list[str] = []
    here_strings: list[str] = []
    has_heredoc = "<<" in args or "<<-" in args
    i = 0
    options_done = False

    def value_after(k: int) -> str:
        return args[k + 1] if k + 1 < len(args) else ""

    while i < len(args):
        t = args[i]
        if t in ("<", "<<", "<<-", "<<<", ">", ">>", "<>", ">&", "<&"):
            v = value_after(i)
            if t == "<":
                stdin_files.append(v)
            elif t == "<<<":
                here_strings.append(v)
            i += 2
            continue
        if options_done:
            i += 1
            continue
        if t == "--":
            options_done = True
        elif t.startswith("--message="):
            check_message(t[len("--message=") :], "commit message")
        elif t == "--message":
            check_message(value_after(i), "commit message")
            i += 1
        elif t.startswith("--trailer="):
            check_trailer(t[len("--trailer=") :])
        elif t == "--trailer":
            check_trailer(value_after(i))
            i += 1
        elif t.startswith("--file="):
            files.append(t[len("--file=") :])
        elif t == "--file":
            files.append(value_after(i))
            i += 1
        elif t.startswith("--"):
            if "=" not in t and t in LONG_VALUE:
                i += 1
        elif t.startswith("-") and len(t) > 1:
            for j, c in enumerate(t[1:], 1):
                if c in "mF" or c in SHORT_VALUE:
                    value = t[j + 1 :]
                    if not value:
                        i += 1
                        value = args[i] if i < len(args) else ""
                    if c == "m":
                        check_message(value, "commit message")
                    elif c == "F":
                        files.append(value)
                    break
                if c in SHORT_ATTACHED:
                    break
        i += 1

    for s in here_strings:
        check_message(s, "here-string message")
    for f in files:
        if f != "-":
            read_message_file(f, base, "-F message file")
            continue
        # stdin: a heredoc (its body is in the command text, already scanned) or a literal `< file`
        if stdin_files:
            for sf in stdin_files:
                read_message_file(sf, cwd, "stdin message file")
        elif not has_heredoc and not here_strings:
            raise RefusalError(
                "cannot verify an -F - message read from a pipe or an unknown stdin; "
                "use -m, a quoted heredoc, or -F <file>."
            )


def check_trailer(value: str) -> None:
    if TRAILER_KEY.search(value):
        raise RefusalError("the --trailer adds a Co-Authored-By trailer.")
    if EXPANSION.search(value):
        raise RefusalError(
            f"cannot verify the --trailer value '{value[:60]}': it holds a shell expansion."
        )


def git_base(opts: list[str], cwd: str) -> str | None:
    base: str | None = cwd
    k = 0
    while k < len(opts):
        if opts[k] == "-C":
            d = opts[k + 1] if k + 1 < len(opts) else ""
            if not d or EXPANSION.search(d) or base is None:
                base = None
            else:
                base = os.path.join(base, os.path.expanduser(d))
            k += 2
        elif opts[k] in GIT_VALUE_OPTS:
            k += 2
        else:
            k += 1
    return base


def why(cmd: str, cwd: str) -> str:
    text = cmd.replace("\\\n", "")
    text = decode_ansi_c(text)
    # The whole text, escapes decoded too: a heredoc body, a printf format, an `-m` with newlines.
    if TRAILER_LINE.search(text) or TRAILER_LINE.search(text.replace("\\n", "\n")):
        return "the commit message carries a Co-Authored-By trailer."
    stripped = protect_literals(strip_heredoc_bodies(text))
    # Every `git` token, not only a statement's first: a comment swallows the newline after it, so
    # two commands can share one token list, and a false match only checks more (the safe way).
    for stmt in statements(tokens(stripped)):
        for g, t in enumerate(stmt):
            if t != "git" and not t.endswith("/git"):
                continue
            k = g + 1
            while k < len(stmt) and stmt[k].startswith("-"):
                k += 2 if stmt[k] in GIT_VALUE_OPTS else 1
            if k < len(stmt) and stmt[k] == "commit":
                check_commit(stmt[k + 1 :], git_base(stmt[g + 1 : k], cwd), cwd)
    return ""


def main() -> int:
    data = json.load(sys.stdin)
    cmd = data.get("tool_input", {}).get("command", "")
    cwd = data.get("cwd") or "."
    if not isinstance(cmd, str) or not isinstance(cwd, str):
        print("the hook input is not a command string; refusing rather than guessing.")
        return 0
    try:
        print(why(cmd, cwd))
    except RefusalError as r:
        print(r)
    return 0


if __name__ == "__main__":
    sys.exit(main())
