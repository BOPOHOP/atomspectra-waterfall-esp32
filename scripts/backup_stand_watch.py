#!/usr/bin/env python3
"""Стенд issue #52: наблюдение за автоматическими снимками на живой плате.

Опрашивает /api/list с заданным периодом и печатает, какие снимки лежат на
плате в каждый момент. Ничего не настраивает и не удаляет — только читает,
чтобы наблюдение не влияло на проверяемое поведение.

    python scripts/backup_stand_watch.py <ip-платы> --minutes 9 --every 30

Вывод — по строке на опрос: время, число снимков, их имена. В конце —
сводка: какие имена появлялись и какие исчезли (это и есть факт ротации).
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.request

sys.stdout.reconfigure(encoding="utf-8")


def fetch_list(ip: str, timeout: float = 8.0) -> dict:
    with urllib.request.urlopen(f"http://{ip}/api/list", timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("ip")
    ap.add_argument("--minutes", type=float, default=9.0)
    ap.add_argument("--every", type=float, default=30.0)
    a = ap.parse_args()

    deadline = time.time() + a.minutes * 60
    seen: set[str] = set()          # все имена, что когда-либо видели
    gone: list[str] = []            # исчезнувшие = вытесненные ротацией
    prev: set[str] = set()
    first = True

    while time.time() < deadline:
        try:
            d = fetch_list(a.ip)
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            print(f"{time.strftime('%H:%M:%S')}  ОШИБКА опроса: {e}", flush=True)
            time.sleep(a.every)
            continue
        names = {b["name"] for b in d.get("backups", [])}
        if not first:
            for n in sorted(prev - names):
                gone.append(n)
                print(f"{time.strftime('%H:%M:%S')}  ВЫТЕСНЕН ротацией: {n}", flush=True)
        for n in sorted(names - prev):
            print(f"{time.strftime('%H:%M:%S')}  ПОЯВИЛСЯ: {n}", flush=True)
        seen |= names
        prev = names
        first = False
        print(f"{time.strftime('%H:%M:%S')}  снимков={len(names)} session={d.get('session')} "
              f"[{', '.join(sorted(names))}]", flush=True)
        time.sleep(a.every)

    print("\n=== ИТОГ ===")
    print(f"видели всего: {len(seen)} — {', '.join(sorted(seen))}")
    print(f"вытеснено:    {len(gone)} — {', '.join(gone) if gone else '(ничего)'}")
    print(f"осталось:     {len(prev)} — {', '.join(sorted(prev))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
