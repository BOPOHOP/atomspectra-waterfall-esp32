#!/usr/bin/env python3
"""Сколько раз плата загружается на одну команду перезагрузки (issue #52).

Счётчик сессий рос на 2, затем на 4 за одну команду /api/reboot-esp. Отличить
«счётчик считает лишнее» от «плата грузится несколько раз» можно только
наблюдением со стороны: uptime_sec падает ровно столько раз, сколько было
загрузок.

    python scripts/boot_count_probe.py <ip-платы> --seconds 120 --every 3

Печатает каждое падение uptime (= новая загрузка) и в конце сводку:
загрузок насчитано N, счётчик сессий вырос на M. N == M — счётчик исправен.
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.request

sys.stdout.reconfigure(encoding="utf-8")


def get_json(url: str, headers: dict | None = None, timeout: float = 6.0):
    req = urllib.request.Request(url, headers=headers or {})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def post(url: str, headers: dict, timeout: float = 6.0):
    req = urllib.request.Request(url, data=b"", headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read()
    except (urllib.error.URLError, TimeoutError, OSError):
        return None      # плата уходит в ребут, ответ может не долететь


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("ip")
    ap.add_argument("--seconds", type=float, default=120.0)
    ap.add_argument("--every", type=float, default=3.0)
    ap.add_argument("--no-reboot", action="store_true",
                    help="только наблюдать, команду перезагрузки не слать")
    a = ap.parse_args()
    base = f"http://{a.ip}"

    sess_before = get_json(f"{base}/api/boot-config")["session"]
    up_prev = get_json(f"{base}/api/system")["uptime_sec"]
    print(f"старт: session={sess_before} uptime={up_prev}", flush=True)

    if not a.no_reboot:
        tok = get_json(f"{base}/api/csrf-token")["token"]
        post(f"{base}/api/reboot-esp", {"X-CSRF-Token": tok})
        print("команда перезагрузки отправлена", flush=True)

    boots = 0
    deadline = time.time() + a.seconds
    while time.time() < deadline:
        time.sleep(a.every)
        try:
            up = get_json(f"{base}/api/system")["uptime_sec"]
        except (urllib.error.URLError, TimeoutError, OSError):
            print(f"{time.strftime('%H:%M:%S')}  плата не отвечает (идёт загрузка)", flush=True)
            continue
        if up_prev is not None and up < up_prev:
            boots += 1
            print(f"{time.strftime('%H:%M:%S')}  ЗАГРУЗКА #{boots}: uptime {up_prev} -> {up}",
                  flush=True)
        up_prev = up

    sess_after = get_json(f"{base}/api/boot-config")["session"]
    grew = sess_after - sess_before
    print("\n=== ИТОГ ===")
    print(f"загрузок замечено (падений uptime): {boots}")
    print(f"счётчик сессий вырос на:            {grew} ({sess_before} -> {sess_after})")
    if boots == grew:
        print("СХОДИТСЯ: счётчик считает загрузки верно")
    else:
        print("НЕ СХОДИТСЯ: счётчик и число загрузок расходятся")
    return 0


if __name__ == "__main__":
    sys.exit(main())
