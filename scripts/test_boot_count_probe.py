#!/usr/bin/env python3
"""Тесты verdict() из boot_count_probe.py + мутанты (плата не нужна).
Запуск: python scripts/test_boot_count_probe.py"""
import pathlib, sys
sys.stdout.reconfigure(encoding="utf-8")
SRC = (pathlib.Path(__file__).parent / "boot_count_probe.py").read_text(encoding="utf-8")

CASES = [  # (boots, before, after, ожидание) — сходимость, расхождение, сброс
    (1, 7, 8, "ok"), (0, 7, 7, "ok"), (2, 7, 9, "ok"),
    (1, 7, 9, "mismatch"), (2, 7, 8, "mismatch"), (1, 7, 7, "mismatch"),
    (1, 7, 1, "reset"), (1, 120, 0, "reset"), (0, 7, 6, "reset"),
]

def failed(src: str) -> list:
    ns = {"__name__": "probe"}
    exec(compile(src, "probe", "exec"), ns)
    return [c for c in CASES if ns["verdict"](*c[:3]) != c[3]] + rc_failed(src)
def main_rc(src: str, after: int) -> int:
    """pass3B #12: код возврата main() при расхождении (плата подменена заглушками)."""
    ns = {"__name__": "probe"}
    exec(compile(src, "probe", "exec"), ns)
    seq = iter([7, after])   # session до и после; uptime — константа, загрузок 0
    ns["get_json"] = lambda url: {"session": next(seq)} if "boot-config" in url else {"uptime_sec": 100}
    old = sys.argv
    sys.argv = ["probe", "1.2.3.4", "--no-reboot", "--seconds", "0"]
    try:
        return ns["main"]()
    finally:
        sys.argv = old

def rc_failed(src: str) -> list:
    return [n for n, a, want in (("mismatch->1", 9, 1), ("ok->0", 7, 0), ("reset->0", 1, 0))
            if main_rc(src, a) != want]
MUTANTS = {
    "mismatch_rc0": ('return 1   # pass3B', 'return 0   # pass3B'),
    "no_reset_branch": ('if sess_after < sess_before:', 'if False:'),
    "reset_le": ('if sess_after < sess_before:', 'if sess_after <= sess_before:'),
    "ok_inverted": ('"ok" if boots == sess_after - sess_before else "mismatch"',
                    '"mismatch" if boots == sess_after - sess_before else "ok"'),
    "ok_loose": ('boots == sess_after - sess_before', 'boots >= sess_after - sess_before'),
}

if __name__ == "__main__":
    bad = failed(SRC)
    print("годный: провалов", len(bad), bad)
    killed = 0
    for name, (a, b) in MUTANTS.items():
        assert a in SRC, name
        f = failed(SRC.replace(a, b, 1))
        killed += bool(f)
        print(f"{name}: провалов {len(f)}")
    print(f"мутантов probe: {killed}/{len(MUTANTS)} поймано")
    sys.exit(1 if bad or killed != len(MUTANTS) else 0)
