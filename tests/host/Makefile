# Host-тесты shproto — обычный gcc/clang, без ESP-IDF.
# Компонент components/shproto зависит только от stdint/stddef/stdbool/string,
# поэтому конечный автомат протокола тестируется на хосте без железа.
#
#   make test   — собрать и прогнать (ждём "ALL PASSED", код возврата 0)
#   make asan   — то же под AddressSanitizer + UBSan (ловит запись за границы)
#   make clean  — убрать бинарники

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 -I../../components/shproto/include -I../../main
SRCS = test_shproto.c test_roundtrip.c ../../components/shproto/shproto.c \
       test_net_time.c ../../main/net_time.c \
       test_debug_log_filter.c \
       test_hist_stage.c ../../main/spectrum_hist_stage.c \
       test_flash_quiet.c \
       test_kv_array.c \
       test_t1_settle.c ../../main/spectrum_t1.c \
       test_wf_seg_clear.c ../../main/wf_seg_delete.c \
       fuzz_backup_name.c \
       test_acq_watch.c \
       test_acq_intent.c \
       test_backup_plan.c ../../main/backup_plan.c \
       test_restore_plan.c \
       test_wifi_reconnect_plan.c \
       test_wifi_return_plan.c \
       test_spectrum_base_plan.c \
       test_http_activity_plan.c \
       test_boot_flag_plan.c \
       test_ota_image_check.c \
       test_ota_mark_valid_plan.c \
       test_json_uint_fmt.c \
       test_ota_github_version.c \
       test_ota_github_sha256sums.c \
       test_ota_github_parse.c \
       test_ota_busy.c \
       test_ota_github_decision.c \
       test_ota_github_redirect.c \
       test_ota_timeout_budget.c \
       test_ota_github_download_retry.c \
       test_calib_autoread.c \
       test_text_accum.c \
       test_sweepA_small.c \
       test_wf_export_plan.c \
       test_wf_seg_rebuild_range.c \
       test_spec_cache_gen.c \
       test_calib_export.c \
       test_text_accum_r3a.c \
       test_text_accum_r3.c \
       test_round3_misc.c

test: $(SRCS) test_util.h
	$(CC) $(CFLAGS) -o test_runner $(SRCS) -lm
	./test_runner

# Дополнительный прогон под санитайзерами: даже если canary-проверка пропустит
# регрессию с записью за границу буфера, ASan поймает её жёстко.
asan: $(SRCS) test_util.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -o test_runner_asan $(SRCS) -lm
	./test_runner_asan

clean:
	rm -f test_runner test_runner_asan bench_json_fmt

# UI-P1: замер до/после старого(vsnprintf-loop)/нового(json_append_uint32_csv)
# форматтера бинов — числа для отчёта, не гейт ALL PASSED.
bench: bench_json_fmt.c ../../main/json_uint_fmt.h
	$(CC) -std=c11 -Wall -Wextra -O2 -I../../main -o bench_json_fmt bench_json_fmt.c
	./bench_json_fmt

.PHONY: test asan clean bench
