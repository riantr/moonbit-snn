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

MBT = os.environ.get("SNN_MBT_DIR", r"D:\src\MiniMax\Projects\MoonBit\moonbit-snn\mbt")
MBTI = os.path.join(MBT, "pkg.generated.mbti")


def load_public():
    """Return (types, free_fns, values) declared in the .mbti."""
    types, free_fns, values = set(), set(), set()
    with open(MBTI, encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r'\s*pub(?:\([^)]*\))?\s+fn(?:\[[^\]]*\])?\s+'
                         r'([A-Za-z_][\w]*)::', line)
            if m:
                types.add(m.group(1))
                continue
            # The `(?:\[[^\]]*\])?` matters: a generic free function is written
            # `pub fn[A] constant(A) -> Loc[A]`, and without the type-parameter
            # list the old pattern demanded whitespace where `[` sits. That
            # silently dropped EVERY generic free function from the allow-list,
            # which surfaced as `The value identifier constant is unbound` the
            # first time a migrated test called one.
            m = re.match(r'\s*pub(?:\([^)]*\))?\s+fn(?:\[[^\]]*\])?\s+'
                         r'([A-Za-z_][\w]*)\s*[(<]', line)
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


# A definition line is one whose FIRST token is a declaration keyword. These are
# the lines `qualify` must not touch: `pub fn TarnetBlock::new(` rewritten to
# `pub fn @snn_mbt.TarnetBlock::new(` is a syntax error, not a reference.
#
# `impl` is deliberately absent. An `impl X {` line names a type that is being
# extended, not defined, and that type is often a local one — but it is also
# frequently a type from another file, which DOES need the prefix. Treating it
# as a definition line would leave those unprefixed; treating it as code would
# wrongly prefix a same-package type. Neither is right, so `impl` lines are
# handled by the caller passing the same-package set (see `qualify`).
_DEF_LINE = re.compile(
    r'^(?:pub(?:\([a-z]+\))?\s+)?'
    r'(?:fn|struct|enum|type|trait|const|let|extern)\b'
)


def is_def_line(text):
    """True when `text` opens a top-level declaration and must not be prefixed.

    Leading whitespace disqualifies the line: indented `fn`s are methods inside
    an `impl` block, and those are already-correct call sites when they invoke
    something, or private helpers when they don't.
    """
    line = text.split("\n", 1)[0]
    return bool(_DEF_LINE.match(line))


def decl_line_starts(src):
    """Byte offsets of the source lines that open a declaration.

    Computed from the raw text, not from `split_code`, because the comment
    scanner yields segments that almost never begin at a line start: a `/// doc`
    comment sits above nearly every definition, so every code segment arrives
    with a leading newline. Matching offsets back onto the original string keeps
    the lossless-split gate intact — nothing here rewrites anything.

    A `struct`/`enum` body is included too, even though its members are
    indented: those members are being declared, not referenced, and prefixing
    them breaks the declaration. `pub enum DendriticTreeType { BallAndStick }`
    became `@neuron.BallAndStick` without this.
    """
    starts = []
    off = 0
    # 缩进超过它的成员都算声明成员；记录当前进入的构造器深度。
    member_indent = None
    for line in src.split("\n"):
        m = _DEF_LINE.match(line)
        if m:
            kw = m.group(0).strip().split()[-1] if m.group(0) else ''
            starts.append(off)
            ind = len(line) - len(line.lstrip())
            if kw in ('struct', 'enum', 'trait'):
                member_indent = ind + 1
            else:
                member_indent = None
        elif member_indent is not None and line.strip():
            ind = len(line) - len(line.lstrip())
            if ind >= member_indent:
                starts.append(off)
            else:
                member_indent = None
        off += len(line) + 1
    return starts


def _span_to(src, offsets):
    """Convert offsets into (start, end) spans, each ending after its line."""
    spans = []
    for o in offsets:
        nl = src.find("\n", o)
        spans.append((o, len(src) if nl < 0 else nl + 1))
    return spans


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
# `let (o2, a2, m2, h2) = ...` — a tuple destructure. The plain BIND pattern sees
# only `let (` and stops, so every bound name inside the parentheses looks like
# a package symbol. `m2` really is one (`pub let m2 : Float`, the millisecond
# squared), and qualifying the local turned a destructuring into "expected
# upper case identifier for type name".
#
# The opening bracket is required, and it is what separates a destructure from
# `let x : Type = ...`: without it the pattern also collected the type
# annotation and exempted `Array`, `SequenceReplayBuffer` and friends from
# qualifying, which quietly un-rewrites real references.
TUPLE_BIND = re.compile(
    r'(?:let\s+mut\s+|let\s+)(\([^=\n]*?\)|\[[^\]\n]*?\])\s*=(?!=)')


# `fn f(\n  name : String,\n)` — a parameter is a local binding for the whole
# body, exactly like a `let`. Without collecting these, a parameter whose name
# collides with a package symbol gets the prefix and the declaration stops
# parsing: `pub fn[A] Tape::op1(self : Tape[A], @core.name : String, ...)`.
_SIGNATURE = re.compile(r'^(?:pub(?:\([a-z]+\))?\s+)?fn(?:\[[^\]]*\])?[^(\n]*\(')


def param_names(lines, idx):
    """Names bound as parameters of the function whose `fn` is at `idx`.

    Walks from the `(` after the function name to its matching `)`, collecting
    the identifier that starts each parameter entry. Handles both the
    `name : Type` and bare `Type` forms; a bare-type parameter cannot collide
    with a value symbol and is skipped.
    """
    text = '\n'.join(lines[idx:])
    m = _SIGNATURE.match(lines[idx])
    if not m:
        return set()
    start = m.end() - 1
    depth, n = 0, len(text)
    for i in range(start, n):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                text = text[start + 1:i]
                break
    else:
        text = text[start + 1:]
    names = set()
    for entry in text.split(','):
        mm = re.match(r'\s*([A-Za-z_][\w]*)\s*(?::|$)', entry)
        if mm:
            names.add(mm.group(1))
    return names


def shadowed_names(src):
    """Names this file binds locally; prefixing them would be a type error."""
    names = set()
    lines = src.split('\n')
    sig_lines = set()
    for i, line in enumerate(lines):
        if _SIGNATURE.match(line):
            sig_lines.add(i)
            names |= param_names(lines, i)
    for is_code, text in split_code(src):
        if not is_code:
            continue
        for m in BIND.finditer(text):
            names.add(m.group(1))
        # Names inside a destructuring pattern: `let (a, b) = ...`, `let [a, b]`.
        for m in TUPLE_BIND.finditer(text):
            pat = m.group(1)
            if not pat or pat.lstrip().startswith("'"):
                continue
            for nm in re.findall(r'[A-Za-z_][\w]*', pat):
                names.add(nm)
    return names


def qualify(src, types, free_fns, values, prefix="@snn_mbt.", own_symbols=None):
    """Rewrite `src` so allow-listed symbols are reached through `prefix`.

    `prefix` is a parameter so one rewriter serves every sub-package: the wave
    test split passes "@snn_mbt.", a future source split passes "@nn." or
    "@rl." for whichever package the symbol lands in. A single symbol can even
    carry its own prefix when `prefix_of` says so.

    `own_symbols` is the set this file itself defines. Those stay bare even if
    they are allow-listed, because after the move they are same-package
    references — prefixing them would be a not-found error against your own
    package.
    """
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
    own = shadow | (own_symbols or set())
    bare = {n for n in (types | free_fns | values) if n not in own}
    # A bare name directly followed by `=` is a named argument or an assignment
    # target, never a reference. `cm` is both a package const and a struct
    # field, so without this the rewriter emits `@snn_mbt.cm=100.0F` and the
    # compiler reads `snn_mbt.cm` as a parameter label. Dropping these from the
    # pattern is cheaper and clearer than undoing the prefix afterwards.
    bare -= _named_argument_labels(src)
    # longest first so `LinearValueNet` beats a hypothetical `Linear`
    order = sorted(bare, key=len, reverse=True)
    t_pat = "|".join(re.escape(n) for n in sorted(bare, key=len, reverse=True)
                     if n in types)
    b_pat = "|".join(re.escape(n) for n in order)

    # Byte ranges of declaration lines. A segment that intersects one keeps its
    # declaration head verbatim; the remainder of that segment is still code.
    def_spans = _span_to(src, decl_line_starts(src))

    stats = {"type_method": 0, "bare": 0, "def_skipped": 0}
    out = []
    pos = 0
    for is_code, text in segments:
        seg_lo, seg_hi = pos, pos + len(text)
        pos = seg_hi
        if not is_code:
            out.append(text)
            continue
        # Cut the segment at every declaration line it contains, rewrite the
        # gaps, and pass each declaration's own head through untouched.
        #
        # Both halves of this used to be wrong in ways that looked plausible.
        # Selecting a span by `sp[0] >= seg_lo` missed the declaration that
        # begins a segment, because a span ends just past its newline and the
        # next segment starts exactly there. Overlap-testing fixed that but
        # then dropped everything after the declaration, because only `tail`
        # was re-emitted. Walking the spans in order and appending the gap
        # after each one keeps the function body — which is where nearly all
        # the call sites are.
        cursor = seg_lo
        for sp_lo, sp_hi in def_spans:
            if sp_lo < cursor or sp_lo >= seg_hi:
                continue
            out.extend(_rewrite(text[cursor - seg_lo:sp_lo - seg_lo],
                                t_pat, b_pat, prefix, stats))
            decl_txt = text[sp_lo - seg_lo:sp_hi - seg_lo]
            head, tail = _split_decl(decl_txt)
            stats["def_skipped"] += 1
            out.append(head)
            out.extend(_rewrite(tail, t_pat, b_pat, prefix, stats))
            cursor = sp_hi
        out.extend(_rewrite(text[cursor - seg_lo:], t_pat, b_pat, prefix,
                            stats))
    return "".join(out), stats


_DECL_SPLIT = re.compile(
    r'^((?:pub(?:\([a-z]+\))?\s+)?(?:fn|struct|enum|type|trait|const|let|extern)\b)'
)


def _split_decl(text):
    """Split a declaration line into (verbatim head, rewritable tail).

    The keyword and the declared name stay as they are — `fn matvec(` must not
    become `fn @snn_mbt.matvec(`. Parameter types, return types and any
    initialiser are ordinary code and are returned for rewriting.
    """
    nl = text.find("\n")
    line = text if nl < 0 else text[:nl + 1]
    rest = "" if nl < 0 else text[nl + 1:]
    m = _DECL_SPLIT.match(line)
    if not m:
        # A bare member line: an enum variant or a struct field, indented with
        # no declaration keyword. The member's own NAME is being declared, so it
        # must stay bare — but its type annotation is ordinary code and does
        # need the prefix. `conns : Array[SpikingSynapse]` inside
        # `pub(all) struct HeterogeneousModel` is exactly this: qualifying the
        # type is required, and qualifying `conns` would be a syntax error.
        vm = re.match(r'^(\s*)([A-Za-z_][\w]*)\s*(:|\()', line)
        if vm and not line.lstrip().startswith(('pub', 'fn', 'let', 'type')):
            return line[:vm.end(2)], line[vm.end(2):]
        return line, rest
    tail = line[m.end():]
    nm = re.match(r'\s*(?:"C"\s+)?[A-Za-z_][\w]*(?:::)?', tail)
    if nm:
        return line[:m.end()] + tail[:nm.end()], tail[nm.end():] + rest
    return line[:m.end()], tail + rest


_NAMED_ARG = re.compile(r'([A-Za-z_][\w]*)\s*=(?!=)')
# `let ph : AdExParameterHet = { ... }` — the name before `=` is a TYPE that
# still has to be qualified, so it cannot simply be blanked out here. Only the
# `=` is masked, which keeps the type visible to Rule 2 while stopping the
# label scan from claiming it.
_TYPE_ANNOTATION = re.compile(r'(\s*:\s*[A-Za-z_][\w]*)(\s*=(?!=))')


def _named_argument_labels(src):
    """Names used as `label=` — a named argument, or an assignment target.

    Scanned in code segments only. A type annotation is masked by removing its
    `=` and keeping the name, because `let ph : AdExParameterHet = {` reads
    exactly like an assignment to `AdExParameterHet`; blanking the whole name
    instead would also drop it from the qualification pattern, and the type
    does need `@snn_mbt.`.
    """
    found = set()
    for is_code, text in split_code(src):
        if not is_code:
            continue
        masked = _TYPE_ANNOTATION.sub(r'\1', text)
        for m in _NAMED_ARG.finditer(masked):
            found.add(m.group(1))
    return found


def _rewrite(text, t_pat, b_pat, prefix, stats):
    """Apply both prefix rules to one code segment."""
    parts = []
    # Rule 1: Type::method  ->  @pkg.Type::method
    if t_pat:
        def r1(m):
            stats["type_method"] += 1
            return prefix + m.group(0)
        text = re.sub(r'(?<![\w.@])(' + t_pat + r')::', r1, text)
    # Rule 2: bare public name -> @pkg.Name
    #   skip after . @ : and before : (that is `::`, already handled)
    if b_pat:
        def r2(m):
            stats["bare"] += 1
            return prefix + m.group(0)
        text = re.sub(r'(?<![\w.@:])(' + b_pat + r')(?![\w:])', r2, text)
    # No undo pass here. `label=` names are already kept out of the pattern by
    # `_named_argument_labels`, and an earlier attempt to also strip prefixes
    # after the fact removed the prefix from `let p : ExtendedIF = ...` — the
    # annotation's `=` looks just like a named argument's, so the undo could not
    # tell them apart and silently un-qualified real type references.
    parts.append(text)
    return parts


def load_private(mbt_dir=None):
    """Return (types, free_fns, values) that exist in the sources but NOT in
    the .mbti.

    The .mbti only carries `pub` items, but the tree has ~200 non-pub top-level
    definitions, and 79 call sites in 46 files reference them across files —
    `xavier_normal` and `matvec` (lstm_cell.mbt), `gln_forward` (layer_norm.mbt),
    `dn_l2` (dragonnet.mbt), and the `extern "C"` primitives `cosf`
    (scheduler.mbt:142) and `sinf` (bottles.mbt:59). Without these in the
    allow-list the rewriter silently skips them and the split then fails to
    compile with not-found errors that point at the wrong file.

    Sources are read here, not the .mbti, because by definition these symbols
    are the ones the .mbti does not carry.
    """
    src_dir = mbt_dir or MBT
    types, free_fns, values = set(), set(), set()
    if not os.path.isdir(src_dir):
        return types, free_fns, values
    pat_type = re.compile(
        r'^(?:pub(?:\([a-z]+\))?\s+)?(?:struct|enum|trait)\s+([A-Z][\w]*)')
    # `fn[A] name(` — the type-parameter list is optional but must be allowed,
    # or every generic definition is invisible to the scanner.
    pat_fn = re.compile(
        r'^(?:pub(?:\([a-z]+\))?\s+)?fn(?:\[[^\]]*\])?\s+([A-Za-z_][\w]*)')
    pat_val = re.compile(
        r'^(?:pub(?:\([a-z]+\))?\s+)?(?:const|let)\s+([A-Za-z_][\w]*)')
    for name in sorted(os.listdir(src_dir)):
        if not name.endswith(".mbt"):
            continue
        with open(os.path.join(src_dir, name), encoding="utf-8") as fh:
            for line in fh:
                # Definitions are column-0. `impl`-block methods are indented
                # and are not top-level symbols.
                if line[:1].isspace() or line[:1] in "}]":
                    continue
                m = pat_fn.match(line)
                if m:
                    free_fns.add(m.group(1))
                    continue
                m = pat_type.match(line)
                if m:
                    types.add(m.group(1))
                    continue
                m = pat_val.match(line)
                if m:
                    values.add(m.group(1))
    return types, free_fns, values


def load_own_symbols(src):
    """Top-level names this file defines; they must stay bare after a move."""
    types, free_fns, values = set(), set(), set()
    pat_type = re.compile(
        r'^(?:pub(?:\([a-z]+\))?\s+)?(?:struct|enum|trait|type|alias)\s+'
        r'([A-Za-z_][\w]*)')
    pat_meth = re.compile(
        r'^(?:pub(?:\([a-z]+\))?\s+)?fn(?:\[[^\]]*\])?\s+([A-Za-z_][\w]*)::')
    pat_fn = re.compile(
        r'^(?:pub(?:\([a-z]+\))?\s+)?fn(?:\[[^\]]*\])?\s+([A-Za-z_][\w]*)')
    pat_ext = re.compile(
        r'^(?:pub(?:\([a-z]+\))?\s+)?extern\s+"[^"]*"\s+fn\s+([A-Za-z_][\w]*)')
    for line in src.split("\n"):
        for pat, bucket in ((pat_meth, types), (pat_type, types),
                            (pat_ext, free_fns), (pat_fn, free_fns),
                            (re.compile(r'^(?:pub(?:\([a-z]+\))?\s+)?'
                                        r'(?:const|let)\s+([A-Za-z_][\w]*)'),
                             values)):
            m = pat.match(line)
            if m:
                bucket.add(m.group(1))
                break
    # Enum variants declared here are local to this file, in every sense that
    # matters for rewriting: `enum DendriticTreeType { BallAndStick }` then
    # `if ds.length() == 1 { BallAndStick }` is a construction of this file's
    # own variant, and prefixing it would point at a same-named type in another
    # package. `struct Tripod` elsewhere is a different symbol entirely.
    lines = src.split("\n")
    in_enum = None
    for i, line in enumerate(lines):
        m = re.match(r'^(?:pub(?:\([a-z]+\))?\s+)?enum\s+([A-Za-z_][\w]*)',
                     line)
        if m:
            in_enum = (len(line) - len(line.lstrip())) + 1
            types.add(m.group(1))
            continue
        if in_enum is not None:
            if not line.strip():
                continue
            ind = len(line) - len(line.lstrip())
            if ind < in_enum:
                in_enum = None
                continue
            vm = re.match(r'^\s*([A-Z][\w]*)\s*(\(|,|//|$)', line)
            if vm:
                types.add(vm.group(1))
    return types | free_fns | values


def main():
    types, free_fns, values = load_public()
    print("public types    =", len(types))
    print("public free fns =", len(free_fns))
    print("public values   =", len(values))
    if "--private" in sys.argv:
        pt, pf, pv = load_private()
        print("private types   =", len(pt - types))
        print("private free fns=", len(pf - free_fns))
        print("private values  =", len(pv - values))
        types |= pt
        free_fns |= pf
        values |= pv
        sys.argv.remove("--private")
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    prefix = "@snn_mbt."
    for a in sys.argv[1:]:
        if a.startswith("--prefix="):
            prefix = a.split("=", 1)[1]
    own = "--own" in sys.argv
    if own:
        print("(own-symbol exemption ON: each file's own definitions stay bare)")
    total_t = total_b = total_d = 0
    for arg in args:
        path = os.path.join(MBT, arg)
        with open(path, encoding="utf-8") as fh:
            src = fh.read()
        new, st = qualify(src, types, free_fns, values, prefix=prefix,
                          own_symbols=load_own_symbols(src) if own else None)
        total_t += st["type_method"]
        total_b += st["bare"]
        total_d += st["def_skipped"]
        print(f"  {arg:<46} Type::={st['type_method']:>4}  bare={st['bare']:>4}"
              f"  defskip={st['def_skipped']:>4}")
    print(f"TOTAL prefixes injected: Type::= {total_t}  bare= {total_b}  "
          f"sum={total_t + total_b}   def-lines skipped= {total_d}")


if __name__ == "__main__":
    main()
