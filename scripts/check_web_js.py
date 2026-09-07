#!/usr/bin/env python3
"""Синтаксическая проверка встроенного JS страниц web/*.html.

Страницы вкомпилированы в образ (EMBED_FILES в main/CMakeLists.txt), поэтому
опечатка в скрипте не ловится ни сборкой, ни тестами — она превращается в
пустую страницу уже на плате. Скрипт вырезает содержимое каждого <script>
без src и прогоняет через `node --check`.

    python scripts/check_web_js.py            # все страницы
    python scripts/check_web_js.py web/x.html # выбранные

Код возврата 0 — все блоки разобраны; 1 — есть ошибка (печатается файл,
номер строки в ИСХОДНОМ файле и сообщение node).
"""
import pathlib
import re
import subprocess
import sys
import tempfile

SCRIPT_RE = re.compile(r"<script(?P<attrs>[^>]*)>(?P<body>.*?)</script>", re.S | re.I)
SRC_RE = re.compile(r"\bsrc\s*=", re.I)


def check_file(path: pathlib.Path) -> list[str]:
    """Возвращает список сообщений об ошибках (пустой = всё разобрано)."""
    text = path.read_text(encoding="utf-8")
    problems: list[str] = []
    for m in SCRIPT_RE.finditer(text):
        if SRC_RE.search(m.group("attrs")):
            continue  # внешний файл — проверяется отдельно как .js
        body = m.group("body")
        if not body.strip():
            continue
        # Номер строки, с которой начинается тело: ошибки node нумеруются от 1
        # внутри блока, а человеку нужен номер в исходном файле.
        base_line = text[: m.start("body")].count("\n") + 1
        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False,
                                         encoding="utf-8") as tmp:
            tmp.write(body)
            tmp_path = pathlib.Path(tmp.name)
        try:
            res = subprocess.run(["node", "--check", str(tmp_path)],
                                 capture_output=True, text=True, encoding="utf-8")
            if res.returncode != 0:
                err = (res.stderr or res.stdout).strip()
                problems.append(f"{path}: блок со строки {base_line}\n{err}")
        finally:
            tmp_path.unlink(missing_ok=True)
    return problems


def main(argv: list[str]) -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    if argv:
        files = [pathlib.Path(a) for a in argv]
    else:
        files = sorted((root / "web").glob("*.html"))
    if not files:
        print("нет файлов для проверки", file=sys.stderr)
        return 1
    all_problems: list[str] = []
    for f in files:
        if not f.exists():
            all_problems.append(f"{f}: файла нет")
            continue
        all_problems.extend(check_file(f))
    if all_problems:
        for p in all_problems:
            print(p)
        print(f"\nОШИБОК: {len(all_problems)}")
        return 1
    print(f"OK: разобрано страниц {len(files)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
