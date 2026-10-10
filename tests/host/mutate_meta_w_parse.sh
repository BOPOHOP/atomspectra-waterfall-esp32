#!/usr/bin/env bash
# З-5 (release-gate 1.2.36 pass1): мутанты meta_w_parse() (main/meta_w_parse.h). Точечный мутант краснит РОВНО названный тест.
#   MW1: снята проверка первой цифры (пустое w= принимается)      -> meta_w_bad
#   MW2: ключ w ищется где угодно (xw=, aw=5&w=7)                 -> meta_w_key
#   MW3: снято насыщение (переполнение uint64 на длинном числе)    -> meta_w_sat
#   MW4: буфер запроса снова 32 байта                              -> meta_w_len
#   MW0 (грубый): ключ никогда не находится                        -> meta_w_ok meta_w_key meta_w_bad meta_w_sat
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
H=../../main/meta_w_parse.h
mt_init "$H"
mt_mut MW1_empty_ok  meta_w_bad  "$H" "/^            if (\*v < '0' || \*v > '9') return META_W_BAD;/d"
mt_mut MW2_any_key   meta_w_key  "$H" "s/if ((p == q || p\[-1\] == '&') \&\& p\[0\] == 'w'/if (p[0] == 'w'/"
mt_mut MW3_no_sat    meta_w_sat  "$H" '/acc = (uint64_t)UINT32_MAX + 1u;/d'
mt_mut MW4_q32       meta_w_len  "$H" 's/#define META_Q_MAX 128/#define META_Q_MAX 32/'
mt_mut MW0_never     "meta_w_ok meta_w_key meta_w_bad meta_w_sat" "$H" "s/p\[0\] == 'w' \&\& p\[1\] == '='/p[0] == 'w' \&\& p[1] == '=' \&\& 0/"
mt_base
mt_done
exit $?
