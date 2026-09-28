#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""#AWF-12: патчер — заменяет ровно одно вхождение old-блока на new-блок."""
import sys
def main():
    target, old_p, new_p = sys.argv[1], sys.argv[2], sys.argv[3]
    text = open(target, "r", encoding="utf-8").read()
    old = open(old_p, "r", encoding="utf-8").read()
    new = open(new_p, "r", encoding="utf-8").read()
    n = text.count(old)
    if n != 1:
        print(f"FAIL: old block found {n} times in {target} (expected 1)")
        return 1
    open(target, "w", encoding="utf-8", newline="\n").write(text.replace(old, new, 1))
    print(f"OK: patched {target}")
    return 0
if __name__ == "__main__":
    sys.exit(main())
