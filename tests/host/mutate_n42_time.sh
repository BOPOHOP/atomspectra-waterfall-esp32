#!/bin/sh
# #AWF-F2: mutation check of n42_time_plan.h. Source is restored. Good build: 0 FAIL; each mutant must be RED.
H=../../main/n42_time_plan.h
cp "$H" /tmp/n42t.orig
trap 'cp /tmp/n42t.orig "$H"' EXIT INT TERM   # restore header on abort
run() { # $1=name $2=sed expr
  cp /tmp/n42t.orig "$H"; sed -i "$2" "$H"
  if cmp -s "$H" /tmp/n42t.orig; then echo "$1: SED NOOP"; return; fi
  gcc -std=c11 -Wall -Wextra -Werror -O2 -o /tmp/n42t_m test_n42_time_plan.c 2>/dev/null || { echo "$1: COMPILE-ERR"; return; }
  out=$(/tmp/n42t_m); echo "$1: rc=$? fails=$(echo "$out" | grep -c '^FAIL')"
}
gcc -std=c11 -Wall -Wextra -Werror -O2 -o /tmp/n42t_m test_n42_time_plan.c && /tmp/n42t_m; echo "good rc=$?"
run M1_no_Z 's/%02dZ"/%02d"/'
run M2_localtime 's/gmtime_r(&tt, &tm)/localtime_r(\&tt, \&tm)/'
run M3_no_mon_plus1 's/tm.tm_mon + 1/tm.tm_mon/'
run M4_year_no_1900 's/tm.tm_year + 1900,/tm.tm_year,/'
run M5_n_lt_20 's/n < 21/n < 20/'
run M6_neg_allowed 's/ || t < 0//'
run M7_null_buf 's/!buf || //'
run M8_space_sep 's/%02dT%02d/%02d %02d/'
run M9_no_year_cap '/tm.tm_year > 9999 - 1900/d'
cp /tmp/n42t.orig "$H"; cmp "$H" /tmp/n42t.orig && echo restored
