#!/usr/bin/env bash
# F-13 (Codeaudit): мутант обязан краснеть в НАЗВАННОЙ тест-функции, а не «где-нибудь».
# stdin — вывод make test; stdout — уникальные имена тест-функций с FAIL, через пробел, по алфавиту.
grep -oE '^FAIL [^:]+:[0-9]+' | while read -r _ fl; do
    f=${fl%%:*}; l=${fl##*:}
    awk -v L="$l" 'NR<=L && /^(static )?void test_[A-Za-z0-9_]*\(/ {fn=$0}
                   NR==L {sub(/^(static )?void /,"",fn); sub(/\(.*/,"",fn); print fn; exit}' "$f"
done | sort -u | paste -sd' ' -
