#!/usr/bin/env python3
"""Static checker for ESP-IDF spectrum.c: verifies s_spectrum writes are under SPEC_LOCK."""
import sys, re, argparse, hashlib
from pathlib import Path

CRLF, LF = '\r\n', '\n'

def strip_comments(src: str) -> str:
    """Replace comments, string/char literals, and preprocessor lines with spaces, preserving length and newlines."""
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            j = i
            while j < n and src[j] != '\n':
                out[j] = ' '
                j += 1
            i = j
        elif c == '/' and i + 1 < n and src[i + 1] == '*':
            j = i
            while j < n - 1 and not (src[j] == '*' and src[j + 1] == '/'):
                if src[j] != '\n':
                    out[j] = ' '
                j += 1
            if j < n - 1:
                out[j] = ' '
                out[j + 1] = ' '
                i = j + 2
            else:
                i = n
        elif c == '"' or c == "'":
            q = c
            out[i] = ' '
            i += 1
            while i < n:
                if src[i] == '\\' and i + 1 < n:
                    out[i] = ' '
                    out[i + 1] = ' '
                    i += 2
                elif src[i] == q:
                    out[i] = ' '
                    i += 1
                    break
                elif src[i] == '\n':
                    break
                else:
                    out[i] = ' '
                    i += 1
        else:
            i += 1
    lines = out
    # Blank preprocessor lines
    result = ''.join(out)
    lines = result.split('\n')
    new_lines = []
    for line in lines:
        if line.lstrip().startswith('#'):
            new_lines.append(' ' * len(line))
        else:
            new_lines.append(line)
    return '\n'.join(new_lines)

def line_of(code: str, off: int) -> int:
    """1-based line number for a character offset."""
    return code.count('\n', 0, off) + 1

def find_functions(code: str):
    """Return list of (name, body_start, body_end) for top-level function bodies."""
    funcs = []
    depth = 0
    i = 0
    n = len(code)
    while i < n:
        c = code[i]
        if c == '{':
            if depth == 0:
                # Check if last non-whitespace before is ')'
                j = i - 1
                while j >= 0 and code[j] in ' \t\n':
                    j -= 1
                if j >= 0 and code[j] == ')':
                    # Find matching '('
                    k = j
                    pd = 0
                    while k >= 0:
                        if code[k] == ')':
                            pd += 1
                        elif code[k] == '(':
                            pd -= 1
                            if pd == 0:
                                break
                        k -= 1
                    if k >= 0:
                        # Find identifier before '('
                        m = re.search(r'([A-Za-z_]\w*)\s*$', code[:k])
                        if m:
                            name = m.group(1)
                            # Find matching '}'
                            bd = 0
                            b = i
                            while b < n:
                                if code[b] == '{':
                                    bd += 1
                                elif code[b] == '}':
                                    bd -= 1
                                    if bd == 0:
                                        break
                                b += 1
                            funcs.append((name, i, b))
                            i = b + 1
                            continue
            depth += 1
        elif c == '}':
            depth -= 1
        i += 1
    return funcs

def walk_body(code: str, start: int, end: int, upto: int = None):
    """Walk function body, tracking lock depth and s_spectrum occurrences.
    Returns (d, lock_lines, occurrences, n_unlock); d is the depth just before the closing
    brace of the body, or at offset `upto` when given."""
    d = 0
    stack = []
    lock_lines = []
    occurrences = []
    n_unlock = 0
    i = start
    n = end + 1
    if upto is not None:
        n = upto
    while i < n:
        c = code[i]
        if c == '{':
            stack.append([d, False])
            i += 1
        elif c == '}':
            if i == end:
                break
            if stack:
                d0, exited = stack.pop()
                if exited:
                    d = d0
            i += 1
        else:
            # Check for SPEC_LOCK / SPEC_UNLOCK
            if code[i:i+9] == 'SPEC_LOCK' and (i + 9 >= n or not re.match(r'\w', code[i + 9])):
                # Check preceded by non-word
                if i == 0 or not re.match(r'\w', code[i - 1]):
                    j = i + 9
                    while j < n and code[j] == ' ':
                        j += 1
                    if j < n and code[j] == '(':
                        d += 1
                        lock_lines.append(line_of(code, i))
                        i = j + 1
                        continue
            if code[i:i+11] == 'SPEC_UNLOCK' and (i + 11 >= n or not re.match(r'\w', code[i + 11])):
                if i == 0 or not re.match(r'\w', code[i - 1]):
                    j = i + 11
                    while j < n and code[j] == ' ':
                        j += 1
                    if j < n and code[j] == '(':
                        d -= 1
                        n_unlock += 1
                        i = j + 1
                        continue
            # Check for s_spectrum
            if code[i:i+10] == 's_spectrum' and (i + 10 >= n or not re.match(r'\w', code[i + 10])):
                if i == 0 or not re.match(r'\w', code[i - 1]):
                    occurrences.append((i, d))
                    i += 10
                    continue
            # Check for return/break/continue/goto
            for kw in ('return', 'break', 'continue', 'goto'):
                if code[i:i+len(kw)] == kw and (i + len(kw) >= n or not re.match(r'\w', code[i + len(kw)])):
                    if i == 0 or not re.match(r'\w', code[i - 1]):
                        # Find ';'
                        k = i + len(kw)
                        while k < n and code[k] != ';':
                            k += 1
                        if k < n:
                            # Check next non-whitespace after ';'
                            m = k + 1
                            while m < n and code[m] in ' \t\n':
                                m += 1
                            if m < n and code[m] == '}' and stack:
                                stack[-1][1] = True
                        break
            i += 1
    return d, lock_lines, occurrences, n_unlock

def classify(code: str, off: int, d: int, fname: str, allow_fns: set):
    """Classify an s_spectrum occurrence as WRITE, ESCAPE, or READ."""
    # Find line boundaries
    ls = code.rfind('\n', 0, off) + 1
    le = code.find('\n', off)
    if le == -1:
        le = len(code)
    before = code[ls:off]
    after = code[off + 10:le]
    
    # Consume member chain
    chain = ''
    rest = after
    while True:
        m = re.match(r'\s*\.\s*\w+', rest)
        if m:
            chain += m.group(0)
            rest = rest[m.end():]
            continue
        m = re.match(r'\s*\[[^\]]*\]', rest)
        if m:
            chain += m.group(0)
            rest = rest[m.end():]
            continue
        break
    
    # Check if inside sizeof
    if re.search(r'sizeof\s*\(\s*$', before):
        return 'READ'
    
    # Check WRITE conditions
    if re.match(r'^\s*(=(?!=)|\+=|-=|\*=|/=|%=|\|=|&=|\^=|<<=|>>=|\+\+|--)', rest):
        return 'WRITE'
    if re.search(r'(\+\+|--)\s*$', before):
        return 'WRITE'
    
    # Check if in first arg of specific calls
    callee = None
    first_arg = False
    arg_idx = 0
    k = off - 1
    pd = 0
    while k >= 0:
        if code[k] == ')':
            pd += 1
        elif code[k] == '(':
            if pd == 0:
                # Found the call paren
                m = re.search(r'([A-Za-z_]\w*)\s*$', code[:k])
                if m:
                    callee = m.group(1)
                    # Check if first arg
                    arg_start = k + 1
                    arg_end = off
                    arg_text = code[arg_start:arg_end]
                    cd = 0
                    for ch in arg_text:
                        if ch == '(':
                            cd += 1
                        elif ch == ')':
                            cd -= 1
                        elif ch == ',' and cd == 0:
                            arg_idx += 1
                    first_arg = arg_idx == 0
                break
            pd -= 1
        k -= 1
    
    write_calls = ('memset', 'memcpy', 'memmove', 'strncpy', 'strcpy', 'strcat', 'strncat', 'snprintf', 'sprintf', 'fread')
    if callee in write_calls and first_arg:
        return 'WRITE'
    
    # read-only source argument: memcpy/memmove/memcmp (2nd+), seq_copy / spec_epoch_snapshot_seq src (3rd, const void *)
    src_arg = (callee in ('memcpy', 'memmove', 'memcmp') and not first_arg) or (callee in ('seq_copy', 'spec_epoch_snapshot_seq') and arg_idx == 2)
    # Check ESCAPE conditions ((?<!&): '&&' is logical AND, not address-of)
    if re.search(r'(?<!&)&\s*(\([^()]*\)\s*)?$', before):
        if not src_arg:
            return 'ESCAPE'
    
    if chain.endswith('.bins'):
        if not src_arg:
            if not rest.startswith('['):
                if not re.search(r'sizeof\s*\(\s*$', before):
                    return 'ESCAPE'
    
    return 'READ'

def resolve_callers(code: str, funcs: list, fname: str, depth: int = 0, visited: set = None):
    """Resolve callers of fname, return (status, evidence)."""
    if visited is None:
        visited = set()
    if fname in visited or depth > 4:
        return 'UNLOCKED', 'cycle'
    visited.add(fname)
    
    call_sites = []
    for cname, cstart, cend in funcs:
        if cname == fname:
            continue
        # Find calls to fname in this function
        i = cstart
        while i < cend:
            if code[i:i+len(fname)] == fname and (i + len(fname) >= cend or not re.match(r'\w', code[i + len(fname)])):
                if i == cstart or not re.match(r'\w', code[i - 1]):
                    j = i + len(fname)
                    while j < cend and code[j] == ' ':
                        j += 1
                    if j < cend and code[j] == '(':
                        d_at = walk_body(code, cstart, cend, upto=i)[0]
                        call_sites.append((cname, line_of(code, i), d_at))
                        i += len(fname)
                        continue
            i += 1
    
    if not call_sites:
        return 'UNLOCKED', 'no callers'
    
    all_ok = True
    evidence = []
    for caller, line, d_at in call_sites:
        if d_at >= 1:
            evidence.append(f'{caller}@{line}(d={d_at})')
        else:
            status, ev = resolve_callers(code, funcs, caller, depth + 1, visited.copy())
            if status != 'LOCKED' and status != 'VIA_CALLERS':
                all_ok = False
                evidence.append(f'{caller}@{line}({ev})')
            else:
                evidence.append(f'{caller}@{line}({ev})')
    
    if all_ok:
        return 'VIA_CALLERS', '; '.join(evidence)
    else:
        return 'UNLOCKED', '; '.join(evidence)

def main():
    sys.stdout.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser()
    parser.add_argument('file')
    parser.add_argument('--scan-dir')
    parser.add_argument('--allow-fn', action='append', default=[])
    parser.add_argument('--const-getter', action='append', default=[],
                        help='function returning const pointer to s_spectrum; valid only while step 8 finds 0 casts')
    args = parser.parse_args()
    
    path = Path(args.file)
    raw = path.read_bytes()
    sha = hashlib.sha256(raw).hexdigest()
    src = raw.decode('utf-8', errors='replace').replace(CRLF, LF)   # CRLF working copy on Windows
    code = strip_comments(src)
    n_lines = src.count('\n') + 1
    
    funcs = find_functions(code)
    allow_fns = set(args.allow_fn)
    
    rows = []
    func_info = {}
    n_read_total = [0]
    for name, start, end in funcs:
        d_final, lock_lines, occurrences, n_unl = walk_body(code, start, end)
        balanced = d_final == 0
        func_info[name] = (len(lock_lines), balanced, n_unl)
        for off, d in occurrences:
            cls = classify(code, off, d, name, allow_fns)
            if cls == 'READ':
                n_read_total[0] += 1
            if cls in ('WRITE', 'ESCAPE'):
                line = line_of(code, off)
                if d >= 1:
                    status = 'LOCKED'
                    ev = f'lock@{max([l for l in lock_lines if l <= line], default=1)}'
                elif name in allow_fns:
                    status = 'INIT'
                    ev = 'allow-fn'
                elif name in args.const_getter and cls == 'ESCAPE':
                    status = 'CONST_RO'
                    ev = 'const pointer, users scanned (--scan-dir), see casts='
                else:
                    status, ev = resolve_callers(code, funcs, name)
                raw_line = src.split('\n')[line - 1].strip()[:140]
                rows.append((line, name, cls, d, status, ev, raw_line))
    
    rows.sort(key=lambda x: x[0])
    
    print('# spec_writers.py report')
    print(f'# file: {args.file}  sha256: {sha}  lines: {n_lines}')
    print('# rule: each WRITE/ESCAPE of s_spectrum must be under SPEC_LOCK (d>=1), or reached only from call sites under SPEC_LOCK (VIA_CALLERS), or in a --allow-fn function (INIT)')
    
    for line, name, cls, d, status, ev, raw_line in rows:
        print(f'{line}\t{name}\t{cls}\td={d}\t{status}\t{ev}\t{raw_line}')
    
    print()
    for name, (lock_count, balanced, n_unl) in func_info.items():
        if lock_count > 0 or n_unl > 0:
            print(f'# functions with SPEC_LOCK: {name}: lock={lock_count} unlock={n_unl} balanced={"yes" if balanced else "NO"}')
    
    n_write = sum(1 for r in rows if r[2] == 'WRITE')
    n_escape = sum(1 for r in rows if r[2] == 'ESCAPE')
    n_read = n_read_total[0]
    n_locked = sum(1 for r in rows if r[4] == 'LOCKED')
    n_via = sum(1 for r in rows if r[4] == 'VIA_CALLERS')
    n_init = sum(1 for r in rows if r[4] == 'INIT')
    n_ro = sum(1 for r in rows if r[4] == 'CONST_RO')
    n_unlocked = sum(1 for r in rows if r[4] == 'UNLOCKED')
    n_unbalanced = sum(1 for name, (_, balanced, _u) in func_info.items() if not balanced)
    
    casts = 0
    if args.scan_dir:
        scan_dir = Path(args.scan_dir)
        for f in sorted(scan_dir.glob('*.c')):
            if f.resolve() == path.resolve():
                continue
            fsrc = f.read_bytes().decode('utf-8', errors='replace').replace(CRLF, LF)
            fcode = strip_comments(fsrc)
            flines = fcode.split('\n')
            for idx, fl in enumerate(flines):
                if 'spectrum_get_current' in fl:
                    print(f'# getter-use {f}:{idx + 1}: {fl.strip()}')
                    if re.search(r'\(\s*spectrum_data_t\s*\*\s*\)', fl):
                        print(f'# CAST-TO-NONCONST {f}:{idx + 1}')
                        casts += 1
    
    summary = f'# summary: WRITE={n_write} ESCAPE={n_escape} READ={n_read} LOCKED={n_locked} VIA_CALLERS={n_via} INIT={n_init} CONST_RO={n_ro} UNLOCKED={n_unlocked} UNBALANCED={n_unbalanced}'
    if args.scan_dir:
        summary += f' casts={casts}'
    print(summary)
    
    if n_unlocked > 0 or n_unbalanced > 0 or casts > 0 or (n_ro > 0 and not args.scan_dir):
        sys.exit(1)
    sys.exit(0)

if __name__ == '__main__':
    main()
