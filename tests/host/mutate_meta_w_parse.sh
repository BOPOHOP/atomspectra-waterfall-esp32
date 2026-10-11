#!/usr/bin/env bash
# З-5 (release-gate 1.2.36 pass1): мутанты meta_w_parse() (main/meta_w_parse.h). Точечный мутант краснит РОВНО названный тест.
#   MW1: снята проверка пустого значения (w= принимается как 0)   -> meta_w_bad
#   MW5/MW6/MW7 (pass2 З-1): обрезанный буфером w принят / обрезка по ключу не замечена / нецелое при обрезке -> meta_w_cut
#   MW2: ключ w ищется где угодно (xw=, aw=5&w=7)                 -> meta_w_key
#   MW3: снято насыщение (переполнение uint64 на длинном числе)    -> meta_w_sat
#   MW4: буфер запроса снова 32 байта                              -> meta_w_len
#   MW0 (грубый): ключ никогда не находится                        -> meta_w_ok meta_w_key meta_w_bad meta_w_sat
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
H=../../main/meta_w_parse.h
mt_init "$H"
mt_mut MW1_empty_ok  meta_w_bad  "$H" '/if (v == p + 2) return META_W_BAD;/d'
mt_mut MW2_any_key   meta_w_key  "$H" "s/if ((p == q || p\[-1\] == '&') \&\& p\[0\] == 'w'/if (p[0] == 'w'/"
mt_mut MW3_no_sat    meta_w_sat  "$H" '/acc = (uint64_t)UINT32_MAX + 1u;/d'
mt_mut MW4_q32       meta_w_len  "$H" 's/#define META_Q_MAX 128/#define META_Q_MAX 32/'
mt_mut MW0_never     "meta_w_ok meta_w_key meta_w_bad meta_w_sat meta_w_cut" "$H" "s/p\[0\] == 'w' \&\& p\[1\] == '='/p[0] == 'w' \&\& p[1] == '=' \&\& 0/"
# pass2 З-1: обрезка буфером - значение w до места обрезки принято / обрезка по ключу не замечена / нецелое при обрезке не BAD -> meta_w_cut
mt_mut MW5_cut_value_ok meta_w_cut "$H" 's/if (\*v == .\\0. \&\& trunc) return META_W_CUT;/if (0) return META_W_CUT;/'
mt_mut MW6_cut_key_lost meta_w_cut "$H" '/the cut went through the key itself/{n;d}'
mt_mut MW7_bad_when_cut meta_w_cut "$H" 's/if (!digits) return META_W_BAD;/if (!digits \&\& !trunc) return META_W_BAD;/'
mt_base
mt_done
exit $?
