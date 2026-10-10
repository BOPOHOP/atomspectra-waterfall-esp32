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
       test_round3_misc.c \
       test_wf_ref_plan.c \
       test_session_plan.c \
       test_wf_rst_keep.c \
       test_wf_tail_plan.c \
       test_ota_gh_check_state.c \
       test_debug_log_chunk.c \
       test_spectrum_import_plan.c \
       test_live_time.c \
       test_spectrum_calib_meta.c \
       test_autosave_skip_plan.c \
       test_ttl_plan.c \
       test_spectrum_meta_fmt.c \
       test_seq_copy.c \
       test_monitor_window.c \
       test_autosave_mark_retry.c

test: $(SRCS) test_util.h test_import_body_plan test_import_tmp_plan test_n42_time_plan test_glue_cache test_glue_monitor
	$(CC) $(CFLAGS) -o test_runner $(SRCS) -lm
	./test_runner
	./test_import_body_plan
	./test_import_tmp_plan
	./test_n42_time_plan
	./test_glue_cache
	./test_glue_monitor

# release-gate 1.2.36 pass1 С-2: тесты СКЛЕЙКИ - сам main/*.c (не копия) с заглушками IDF/FreeRTOS из glue/ (учёт блоков glue_alloc).
GLUE_DEPS = glue/glue_stubs.c glue/*.h glue/freertos/*.h
test_glue_cache: test_glue_cache.c glue/glue_cache_env.h $(GLUE_DEPS) ../../main/spectrum_http_cache.c ../../main/spectrum_meta_fmt.h ../../main/spectrum_t1.c
	$(CC) $(CFLAGS) -Iglue -I. -o test_glue_cache test_glue_cache.c glue/glue_stubs.c ../../main/spectrum_t1.c -lm

test_glue_monitor: test_glue_monitor.c $(GLUE_DEPS) ../../main/monitor.c ../../main/monitor_window.h
	$(CC) $(CFLAGS) -Iglue -I. -o test_glue_monitor test_glue_monitor.c glue/glue_stubs.c -lm

# #AWF-F4: автономный тест со своим main() (import_tmp_plan.h) — какой файл SPEC_DIR чистить на старте.
test_import_tmp_plan: test_import_tmp_plan.c ../../main/import_tmp_plan.h
	$(CC) $(CFLAGS) -o test_import_tmp_plan test_import_tmp_plan.c

# AWF-F2: standalone test (n42_time_plan.h) - N42 StartDateTime in UTC with Z.
test_n42_time_plan: test_n42_time_plan.c ../../main/n42_time_plan.h
	$(CC) $(CFLAGS) -o test_n42_time_plan test_n42_time_plan.c

# Автономный тест со своим main() (import_body_plan.h) — в общий test_runner не линкуется.
test_import_body_plan: test_import_body_plan.c ../../main/import_body_plan.h
	$(CC) $(CFLAGS) -o test_import_body_plan test_import_body_plan.c

# Дополнительный прогон под санитайзерами: даже если canary-проверка пропустит
# регрессию с записью за границу буфера, ASan поймает её жёстко.
asan: $(SRCS) test_util.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -o test_runner_asan $(SRCS) -lm
	./test_runner_asan

# 1.2.36 H1 (A): многопоточный стресс seqlock (pthread): писатель под мьютексом с теми же seq++/барьерами,
# что SPEC_LOCK/SPEC_UNLOCK; читатели - seq_copy() без мьютекса; каждый принятый снимок total == сумма bins.
# Отдельно от test_runner: MA1/MA2 (mutate_seq_copy.sh) обязаны краснить его своим прогоном.
stress: test_seq_stress.c ../../main/seq_copy.h
	$(CC) $(CFLAGS) -pthread -o test_seq_stress test_seq_stress.c
	./test_seq_stress

# То же под ThreadSanitizer: гонка копии с писателем ОЖИДАЕМА (риск R-1, копия отбрасывается проверкой seq);
# отчёты не валят прогон (exitcode=0) - их места сводит run-лог.
tsan: test_seq_stress.c ../../main/seq_copy.h
	$(CC) $(CFLAGS) -pthread -fsanitize=thread -g -o test_seq_stress_tsan test_seq_stress.c
	TSAN_OPTIONS="halt_on_error=0 exitcode=0" ./test_seq_stress_tsan

clean:
	rm -f test_runner test_runner_asan bench_json_fmt test_import_body_plan test_import_tmp_plan test_n42_time_plan test_seq_stress test_seq_stress_tsan test_glue_cache test_glue_monitor test_glue_cache_oom

# UI-P1: замер до/после старого(vsnprintf-loop)/нового(json_append_uint32_csv)
# форматтера бинов — числа для отчёта, не гейт ALL PASSED.
bench: bench_json_fmt.c ../../main/json_uint_fmt.h
	$(CC) -std=c11 -Wall -Wextra -O2 -I../../main -o bench_json_fmt bench_json_fmt.c
	./bench_json_fmt

.PHONY: test asan clean bench stress tsan
