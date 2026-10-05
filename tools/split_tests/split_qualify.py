#!/usr/bin/env python3 -X utf8
"""Rewrite a test file so every parent-package symbol is reached as @snn_mbt.X.

The allow-list is `pkg.generated.mbti` (produced by `moon info`), NOT a regex
over the sources: earlier attempt at the latter counted `impl`-block methods as
top-level definitions and reported 233/234 test files as untouchable, which was
an artifact of `^\\s*` matching indented lines.
"""
import os
import re
import sys

MBT = r"D:\src\MiniMax\Projects\MoonBit\moonbit-snn\mbt"
MBTI = os.path.join(MBT, "pkg.generated.mbti")


def load_public():
    """Return (types, free_fns, values) declared in the .mbti."""
    types, free_fns, values = set(), set(), set()
    with open(MBTI, encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r'\s*pub(?:\([^)]*\))?\s+fn\s+([A-Za-z_][\w]*)::', line)
            if m:
                types.add(m.group(1))
                continue
            m = re.match(r'\s*pub(?:\([^)]*\))?\s+fn\s+([A-Za-z_][\w]*)\s*[(<]', line)
            if m:
                free_fns.add(m.group(1))
                continue
            m = re.match(r'\s*pub(?:\([^)]*\))?\s+(?:struct|enum|type|trait|alias)\s+([A-Z][\w]*)', line)
            if m:
                types.add(m.group(1))
                continue
            m = re.match(r'\s*pub(?:\([^)]*\))?\s+(?:const|let)\s+([A-Za-z_][\w]*)', line)
            if m:
                values.add(m.group(1))
    return types, free_fns, values


def split_code(src):
    """Yield (is_code, text) segments, honouring line/block comments and strings.

    MoonBit string interpolation (`\\{expr}`) holds *real code*, so it is
    emitted as a code segment rather than swallowed with the literal. Skipping
    it silently left every interpolated call unbound (e.g. `gelu_grad` inside
    `fail("... \\{gelu_grad(1.0F)} ...")`).
    """
    out, i, n = [], 0, len(src)
    start = 0

    def flush(to, is_code):
        if to > start:
            out.append((is_code, src[start:to]))

    while i < n:
        c = src[i]
        if c == '"':
            flush(i, True)
            # `lit` must stay on the opening quote: the scanner advances past it
            # to look for the terminator, but the quote is part of the literal
            # and dropping it turns `test "name" {` into `test name" {`.
            lit = i
            i += 1
            while i < n:
                if src[i] == '\\':
                    if i + 1 < n and src[i + 1] == '{':
                        # interpolation: literal up to the backslash, then the
                        # braces are code so the expression inside gets
                        # rewritten. The backslash itself is literal text and
                        # must survive, otherwise `\{x}` degrades to `{x}`.
                        out.append((False, src[lit:i]))
                        out.append((False, "\\"))
                        out.append((True, "{"))
                        j = _match_brace(src, i + 2)
                        out.append((True, src[i + 2:j]))
                        out.append((True, "}"))
                        i = lit = j + 1
                        continue
                    i += 2
                    continue
                if src[i] == '"':
                    i += 1
                    break
                i += 1
            out.append((False, src[lit:i]))
            start = i
            continue
        if src.startswith("//", i):
            flush(i, True)
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append((False, src[i:j]))
            i = start = j
            continue
        if src.startswith("/*", i):
            flush(i, True)
            j = src.find("*/", i)
            j = n if j < 0 else j + 2
            out.append((False, src[i:j]))
            i = start = j
            continue
        i += 1
    out.append((True, src[start:]))
    return out


def _match_brace(src, i):
    """Index just past the `}` matching the `{` that sits just before `i`."""
    depth, n = 1, len(src)
    while i < n:
        c = src[i]
        if c == '"':
            i += 1
            while i < n:
                if src[i] == '\\':
                    i += 2
                    continue
                if src[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return n



BIND = re.compile(
    r'(?:^|[^\w.])'
    r'(?:let\s+mut\s+|let\s+|mut\s+|fn\s+|for\s+)'
    r'([A-Za-z_][\w]*)')


def shadowed_names(src):
    """Names this file binds locally; prefixing them would be a type error."""
    names = set()
    for is_code, text in split_code(src):
        if is_code:
            for m in BIND.finditer(text):
                names.add(m.group(1))
    return names


def qualify(src, types, free_fns, values):
    # Gate: the splitter must be a faithful partition of the source. Every bug
    # found so far in this file was a splitter that silently ate a character
    # (a string's opening quote, the backslash of an interpolation escape), and
    # a lost character only shows up much later as an unrelated parse error.
    segments = split_code(src)
    if "".join(t for _, t in segments) != src:
        raise AssertionError(
            "split_code is not lossless; refusing to rewrite a file it would "
            "corrupt")
    shadow = shadowed_names(src)
    bare = {n for n in (types | free_fns | values) if n not in shadow}
    # longest first so `LinearValueNet` beats a hypothetical `Linear`
    order = sorted(bare, key=len, reverse=True)
    t_pat = "|".join(re.escape(n) for n in sorted(types, key=len, reverse=True))
    b_pat = "|".join(re.escape(n) for n in order)

    stats = {"type_method": 0, "bare": 0}
    out = []
    for is_code, text in segments:
        if not is_code:
            out.append(text)
            continue
        # Rule 1: Type::method  ->  @snn_mbt.Type::method
        def r1(m):
            stats["type_method"] += 1
            return "@snn_mbt." + m.group(0)
        text = re.sub(r'(?<![\w.@])(' + t_pat + r')::', r1, text)
        # Rule 2: bare public name -> @snn_mbt.Name
        #   skip after . @ : and before : (that is `::`, already handled)
        def r2(m):
            stats["bare"] += 1
            return "@snn_mbt." + m.group(0)
        text = re.sub(r'(?<![\w.@:])(' + b_pat + r')(?![\w:])', r2, text)
        out.append(text)
    return "".join(out), stats


def main():
    types, free_fns, values = load_public()
    print("public types    =", len(types))
    print("public free fns =", len(free_fns))
    print("public values   =", len(values))
    total_t = total_b = 0
    for arg in sys.argv[1:]:
        path = os.path.join(MBT, arg)
        with open(path, encoding="utf-8") as fh:
            src = fh.read()
        new, st = qualify(src, types, free_fns, values)
        total_t += st["type_method"]
        total_b += st["bare"]
        print(f"  {arg:<46} Type::={st['type_method']:>4}  bare={st['bare']:>4}")
    print(f"TOTAL prefixes injected: Type::= {total_t}  bare= {total_b}  sum={total_t + total_b}")


if __name__ == "__main__":
    main()
