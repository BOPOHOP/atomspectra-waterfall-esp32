#!/usr/bin/env bash
# Библиотека для мутационного тестирования.
# Импортируется через `source mutlib.sh`.
# Не вызывает exit на верхнем уровне.

# Глобальные переменные
MT_RC=0
MT_FILES=()
MT_BAKS=()

# Инициализация: запоминаем файлы и создаём бэкапы
mt_init() {
    MT_FILES=()
    MT_BAKS=()
    local f bak
    for f in "$@"; do
        MT_FILES+=("$f")
        bak=$(mktemp)
        cp "$f" "$bak"
        MT_BAKS+=("$bak")
    done
    trap mt_restore EXIT
    MT_RC=0
}

# Восстановление исходных файлов из бэкапов
mt_restore() {
    local i
    for i in "${!MT_FILES[@]}"; do
        if [ -n "${MT_BAKS[$i]:-}" ] && [ -f "${MT_BAKS[$i]}" ]; then
            cp "${MT_BAKS[$i]}" "${MT_FILES[$i]}"
            rm -f "${MT_BAKS[$i]}"
            MT_BAKS[$i]=""
        fi
    done
}

# Откат файлов из бэкапов БЕЗ удаления бэкапов (между мутантами)
mt_reset() {
    local i
    for i in "${!MT_FILES[@]}"; do
        [ -n "${MT_BAKS[$i]:-}" ] && cp "${MT_BAKS[$i]}" "${MT_FILES[$i]}"
    done
}

# Проверка результатов тестов
mt_check() {
    local label="$1"
    local expect="$2"
    
    rm -f test_runner
    local out mrc
    out=$(make test 2>&1); mrc=$?   # pass2 З-4: код make test - бинарь мог упасть без строки FAIL после «ALL PASSED» test_runner

    # Проверка на успешную сборку
    if ! echo "$out" | grep -qE "ALL PASSED|CHECK\(S\) FAILED"; then
        echo "== $label: BUILD FAILED"
        MT_RC=1
        return
    fi
    
    # Извлечение имен тестов (новый стиль)
    local named
    named=$(echo "$out" | grep -E '^FAIL [A-Za-z0-9_]+ [^ :]+:[0-9]+: ' | awk '{print $2}' | sort -u)
    
    # Подсчет общего количества FAIL
    local total
    total=$(echo "$out" | grep -c '^FAIL')
    
    # Подсчет FAIL старого стиля (второй токен содержит двоеточие)
    local unnamed
    unnamed=$(echo "$out" | grep '^FAIL' | grep -vE '^FAIL [A-Za-z0-9_]+ [^ :]+:[0-9]+: ' | wc -l)
    
    # Формирование строки вывода
    local named_str=""
    if [ -n "$named" ]; then
        named_str=$(echo "$named" | tr '\n' ' ' | sed 's/ $//')
    fi
    
    echo "== $label: $total FAIL, tests=[$named_str] unnamed=$unnamed rc=$mrc"
    
    # Вывод INFO строк
    echo "$out" | grep '^INFO ' | sed 's/^/   /'
    
    # Оценка результата
    if [ "$mrc" -ne 0 ] && [ "$total" -eq 0 ]; then
        # pass2 З-4: make test упал (бинарь вернул != 0 / не собрался) без единой строки FAIL - ни baseline, ни мутант не засчитываются
        MT_RC=1
        echo "   VERDICT BAD (make test rc=$mrc without FAIL line)"
    elif [ "$expect" == "baseline" ]; then
        if [ "$total" -eq 0 ]; then
            echo "   VERDICT OK"
        else
            MT_RC=1
            echo "   VERDICT BAD (baseline must have 0 FAIL)"
        fi
    else
        # Разбиваем ожидаемые имена на массив
        local -a expected_arr
        read -r -a expected_arr <<< "$expect"
        
        # Разбиваем фактические имена на массив
        local -a actual_arr
        if [ -n "$named" ]; then
            actual_arr=($named)
        else
            actual_arr=()
        fi
        
        # Сортируем оба массива для сравнения
        local -a sorted_expected
        local -a sorted_actual
        if [ ${#expected_arr[@]} -gt 0 ]; then
            sorted_expected=($(printf '%s\n' "${expected_arr[@]}" | sort))
        else
            sorted_expected=()
        fi
        if [ ${#actual_arr[@]} -gt 0 ]; then
            sorted_actual=($(printf '%s\n' "${actual_arr[@]}" | sort))
        else
            sorted_actual=()
        fi
        
        # Сравнение массивов
        local match=1
        if [ ${#sorted_expected[@]} -ne ${#sorted_actual[@]} ]; then
            match=0
        else
            local i
            for i in "${!sorted_expected[@]}"; do
                if [ "${sorted_expected[$i]}" != "${sorted_actual[$i]}" ]; then
                    match=0
                    break
                fi
            done
        fi
        
        if [ $match -eq 1 ] && [ "$unnamed" -eq 0 ]; then
            echo "   VERDICT OK"
        else
            MT_RC=1
            echo "   VERDICT BAD (want: $expect)"
        fi
    fi
}

# Мутация файла и проверка
mt_mut() {
    local label="$1"
    local expect="$2"
    local file="$3"
    local sed_expr="$4"
    
    # Восстанавливаем файлы перед мутацией
    mt_reset
    
    # Применяем sed
    sed -i "$sed_expr" "$file"
    
    # Проверяем, применилась ли мутация
    local idx=-1
    local i
    for i in "${!MT_FILES[@]}"; do
        if [ "${MT_FILES[$i]}" == "$file" ]; then
            idx=$i
            break
        fi
    done
    
    if [ $idx -eq -1 ]; then
        echo "== $label: FILE NOT FOUND IN INIT LIST"
        MT_RC=1
        mt_reset
        return
    fi
    
    # Если файл не изменился, считаем ошибку
    if cmp -s "${MT_BAKS[$idx]}" "$file"; then
        echo "== $label: SED DID NOT APPLY"
        MT_RC=1
        mt_reset
        return
    fi
    
    # Проверяем результат
    mt_check "$label" "$expect"
    
    # Восстанавливаем файлы после проверки
    mt_reset
}

# Базовая проверка (без мутаций)
mt_base() {
    mt_reset
    mt_check baseline baseline
}

# Завершение работы
mt_done() {
    mt_restore
    return $MT_RC
}
