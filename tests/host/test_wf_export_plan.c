// #FW-19 (sweep-A 1.2.28): чистая логика экспорта n42 полной истории (main/wf_export_plan.h).
// Шапки сегментов — реальные (с файлов прибора v2..v5), серийный номер заменён нулями.
#include "test_util.h"
#include "wf_export_plan.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Реальные заголовки сегментов */
static const char *HDR_V2 = "{\"saved_rows\":         0,\"saved_at\":         0,\"format\":\"atomspectra-waterfall\",\"version\":2,\"channels\":8192,\"dtype\":\"uint16\",\"byte_order\":\"little\",\"row_stride\":16386,\"row_time\":{\"dtype\":\"uint16\",\"unit\":\"sec\",\"offset\":16384},\"interval_sec\":60,\"started_at\":1783157403,\"serial\":\"00000000\",\"calibration\":[3.65433919686524,0.410925651361521,4.48813599969944e-05,-1.27668057645201e-08,1.20234609621895e-12]}";
static const char *HDR_V3 = "{\"saved_rows\":         0,\"saved_at\":         0,\"format\":\"atomspectra-waterfall\",\"version\":3,\"channels\":8192,\"dtype\":\"uint16\",\"byte_order\":\"little\",\"row_stride\":16402,\"row_fields\":[{\"name\":\"spectrum\",\"dtype\":\"uint16\",\"channels\":8192,\"offset\":0},{\"name\":\"duration\",\"dtype\":\"uint16\",\"unit\":\"sec\",\"offset\":16384},{\"name\":\"timestamp\",\"dtype\":\"uint32\",\"unit\":\"unix_sec\",\"offset\":16386},{\"name\":\"latitude\",\"dtype\":\"float32\",\"unit\":\"deg\",\"offset\":16390},{\"name\":\"longitude\",\"dtype\":\"float32\",\"unit\":\"deg\",\"offset\":16394},{\"name\":\"dose_rate\",\"dtype\":\"float32\",\"unit\":\"usv_h\",\"offset\":16398}],\"baseline\":{\"dtype\":\"uint32\",\"channels\":8192,\"byte_order\":\"little\"},\"compressed\":false,\"interval_sec\":60,\"started_at\":1783287149,\"serial\":\"00000000\",\"calibration\":[-2.88786942576384,0.357685723440668,4.06923195185187e-05,-3.86805049021517e-09,4.88163459930891e-13]}";
static const char *HDR_V4 = "{\"saved_rows\":         0,\"saved_at\":         0,\"format\":\"atomspectra-waterfall\",\"version\":4,\"channels\":8192,\"dtype\":\"uint16\",\"byte_order\":\"little\",\"row_stride\":16406,\"row_fields\":[{\"name\":\"spectrum\",\"dtype\":\"uint16\",\"channels\":8192,\"offset\":0},{\"name\":\"duration\",\"dtype\":\"uint16\",\"unit\":\"sec\",\"offset\":16384},{\"name\":\"timestamp\",\"dtype\":\"uint32\",\"unit\":\"unix_sec\",\"offset\":16386},{\"name\":\"latitude\",\"dtype\":\"float32\",\"unit\":\"deg\",\"offset\":16390},{\"name\":\"longitude\",\"dtype\":\"float32\",\"unit\":\"deg\",\"offset\":16394},{\"name\":\"dose_rate\",\"dtype\":\"float32\",\"unit\":\"usv_h\",\"offset\":16398},{\"name\":\"crc32\",\"dtype\":\"uint32\",\"algo\":\"crc32\",\"covers\":16402,\"offset\":16402}],\"baseline\":{\"dtype\":\"uint32\",\"channels\":8192,\"byte_order\":\"little\"},\"compressed\":false,\"seg_seq\":87,\"total_at_open\":87930,\"interval_sec\":5,\"started_at\":1783605408,\"serial\":\"00000000\",\"calibration\":[3.65433919686524,0.410925651361521,4.48813599969944e-05,-1.27668057645201e-08,1.20234609621895e-12]}";
static const char *HDR_V5 = "{\"saved_rows\":         0,\"saved_at\":         0,\"format\":\"atomspectra-waterfall\",\"version\":5,\"channels\":8192,\"dtype\":\"uint16\",\"byte_order\":\"little\",\"row_stride\":16410,\"row_fields\":[{\"name\":\"spectrum\",\"dtype\":\"uint16\",\"channels\":8192,\"offset\":0},{\"name\":\"duration\",\"dtype\":\"uint16\",\"unit\":\"sec\",\"offset\":16384},{\"name\":\"timestamp\",\"dtype\":\"uint32\",\"unit\":\"unix_sec\",\"offset\":16386},{\"name\":\"latitude\",\"dtype\":\"float32\",\"unit\":\"deg\",\"offset\":16390},{\"name\":\"longitude\",\"dtype\":\"float32\",\"unit\":\"deg\",\"offset\":16394},{\"name\":\"dose_rate\",\"dtype\":\"float32\",\"unit\":\"usv_h\",\"offset\":16398},{\"name\":\"temperature\",\"dtype\":\"float32\",\"unit\":\"celsius\",\"offset\":16402},{\"name\":\"crc32\",\"dtype\":\"uint32\",\"algo\":\"crc32\",\"covers\":16406,\"offset\":16406}],\"baseline\":{\"dtype\":\"uint32\",\"channels\":8192,\"byte_order\":\"little\"},\"compressed\":false,\"seg_seq\":1439,\"total_at_open\":209204,\"interval_sec\":5,\"started_at\":1788789363,\"temp_at_open\":25.00,\"serial\":\"00000000\",\"calibration\":[3.65433919686524,0.410925651361521,4.48813599969944e-05,-1.27668057645201e-08,1.20234609621895e-12]}";

/* Вспомогательные функции */
static wf_seg_reg_t R(uint32_t idx, uint32_t rows, bool fin, uint32_t g0, uint32_t g1) {
    wf_seg_reg_t r = {0};
    r.idx = idx;
    r.rows = rows;
    r.finalized = fin;
    r.g0 = g0;
    r.g1 = g1;
    r.valid = true;
    return r;
}

static bool step_is(const wf_exp_step_t *s, uint8_t kind, uint32_t a, uint32_t b) {
    if (s->kind != kind) return false;
    if (s->a != a) return false;
    if (kind == WF_EXP_STEP_SEG) return true;
    return s->b == b;
}

static uint8_t g_row[16410];

void wf_export_plan_suite(void) {
    /* A. Парсинг заголовков */
    {
        wf_seg_hdr_info_t info;
        
        /* V2 */
        wf_seg_hdr_parse(HDR_V2, &info);
        CHECK(info.version == 2);
        CHECK(info.stride == 16386);
        CHECK(info.interval_sec == 60);
        CHECK(info.started_at == 1783157403);

        /* V3 */
        wf_seg_hdr_parse(HDR_V3, &info);
        CHECK(info.version == 3);
        CHECK(info.stride == 16402);
        CHECK(info.interval_sec == 60);
        CHECK(info.started_at == 1783287149);

        /* V4 */
        wf_seg_hdr_parse(HDR_V4, &info);
        CHECK(info.version == 4);
        CHECK(info.stride == 16406);
        CHECK(info.interval_sec == 5);
        CHECK(info.started_at == 1783605408);

        /* V5 */
        wf_seg_hdr_parse(HDR_V5, &info);
        CHECK(info.version == 5);
        CHECK(info.stride == 16410);
        CHECK(info.interval_sec == 5);
        CHECK(info.started_at == 1788789363);

        /* NULL заголовок -> дефолты */
        wf_seg_hdr_parse(NULL, &info);
        CHECK(info.version == 2);
        CHECK(info.stride == WF_ROW_BYTES);
        CHECK(info.interval_sec == 0);
        CHECK(info.started_at == 0);

        /* Невалидные значения -> дефолты */
        wf_seg_hdr_parse("{\"version\":12,\"row_stride\":20000}", &info);
        CHECK(info.version == 2);
        CHECK(info.stride == WF_ROW_BYTES);

        /* Валидные граничные значения */
        wf_seg_hdr_parse("{\"version\":1,\"row_stride\":16384}", &info);
        CHECK(info.version == 1);
        CHECK(info.stride == 16384);

        /* row_stride ниже минимума -> дефолт */
        wf_seg_hdr_parse("{\"row_stride\":16383}", &info);
        CHECK(info.stride == WF_ROW_BYTES);

        /* row_stride на максимуме (16384+128) -> принято */
        wf_seg_hdr_parse("{\"row_stride\":16512}", &info);
        CHECK(info.stride == 16512);

        /* row_stride выше максимума -> дефолт */
        wf_seg_hdr_parse("{\"row_stride\":16513}", &info);
        CHECK(info.stride == WF_ROW_BYTES);

        /* wf_hdr_find_num */
        long long val = 0;   /* -O2 + инлайн: иначе maybe-uninitialized ложно роняет сборку */
        CHECK(wf_hdr_find_num(HDR_V5, "seg_seq", &val) == true);
        CHECK(val == 1439);

        CHECK(wf_hdr_find_num(HDR_V5, "saved_rows", &val) == true);
        CHECK(val == 0);

        CHECK(wf_hdr_find_num(HDR_V5, "nokey", &val) == false);

        CHECK(wf_hdr_find_num("{\"started_at\":x}", "started_at", &val) == false);

        /* Первое вхождение ключа offset */
        CHECK(wf_hdr_find_num(HDR_V5, "offset", &val) == true);
        CHECK(val == 0);
    }

    /* B. Геометрия */
    {
        CHECK(wf_seg_payload_offset(2, 4096) == 4104);
        CHECK(wf_seg_payload_offset(3, 4096) == 4104 + WF_BASELINE_BYTES);
        CHECK(wf_seg_payload_offset(5, 4096) == 36872); /* 4104 + 32768 */
        CHECK(wf_seg_payload_offset(1, 4096) == 4104);

        CHECK(wf_seg_rows_in(36872 + 3*16410, 36872, 16410) == 3);
        CHECK(wf_seg_rows_in(36872 + 3*16410 + 100, 36872, 16410) == 3);
        CHECK(wf_seg_rows_in(36872, 36872, 16410) == 0);
        CHECK(wf_seg_rows_in(100, 36872, 16410) == 0);
        CHECK(wf_seg_rows_in(50000, 0, 0) == 0);

        /* Реальный файл v2 */
        CHECK(wf_seg_rows_in(10818864, 4104, 16386) == 660);
    }

    /* C. Поля строк */
    {
        memset(g_row, 0, sizeof(g_row));
        
        /* dur = 300 (0x012C LE), ts = 0x12345678 LE */
        g_row[16384] = 0x2C; g_row[16385] = 0x01;
        g_row[16386] = 0x78; g_row[16387] = 0x56; 
        g_row[16388] = 0x34; g_row[16389] = 0x12;

        CHECK(wf_row_dur(g_row, 16410) == 300);
        CHECK(wf_row_dur(g_row, 16386) == 300);
        CHECK(wf_row_dur(g_row, 16384) == 0);

        CHECK(wf_row_ts(g_row, 5, 16410) == 0x12345678u);
        CHECK(wf_row_ts(g_row, 3, 16402) == 0x12345678u);
        CHECK(wf_row_ts(g_row, 2, 16386) == 0);
        /* Решает ВЕРСИЯ, а не только длина строки: у v2 поля timestamp нет даже при
           длинном stride (ASWF_FORMAT.md: timestamp — с v3). Синтетический край: у
           реальных v2-файлов stride всегда 16386, мутант «version >= 2» на них латентен. */
        CHECK(wf_row_ts(g_row, 2, 16402) == 0);
        CHECK(wf_row_ts(g_row, 5, 16388) == 0);

        /* Изменение старшего байта ts */
        g_row[16389] = 0xF0;
        CHECK(wf_row_ts(g_row, 5, 16410) == 0xF0345678u);

        /* Эффективная длительность */
        CHECK(wf_exp_eff_dur(0, 60) == 60);
        CHECK(wf_exp_eff_dur(7, 60) == 7);

        /* Начало строки */
        CHECK(wf_exp_row_start(1788789400u, 1788789363, 999) == 1788789400);
        CHECK(wf_exp_row_start(0, 1783157403, 120) == 1783157403 + 120);
        CHECK(wf_exp_row_start(0, 0, 35) == 35);
    }

    /* D. Планирование экспорта */
    {
        wf_exp_step_t o[16];
        int ret;

        /* 1. Пустой реестр, кольцо [0,10) */
        wf_seg_reg_t empty_reg = {0};
        ret = wf_exp_plan(&empty_reg, 0, 0, 10, o, 16);
        CHECK(ret == 1);
        CHECK(step_is(&o[0], WF_EXP_STEP_RING, 0, 10));

        /* 2. Пустой реестр, пустое кольцо */
        ret = wf_exp_plan(&empty_reg, 0, 0, 0, o, 16);
        CHECK(ret == 0);

        /* 3. Старые сегменты, несортированные */
        wf_seg_reg_t reg3[] = {R(7,5,true,0,0), R(0,5,true,0,0), R(3,5,true,0,0)};
        ret = wf_exp_plan(reg3, 3, 0, 0, o, 16);
        CHECK(ret == 3);
        CHECK(step_is(&o[0], WF_EXP_STEP_SEG, 0, 0));
        CHECK(step_is(&o[1], WF_EXP_STEP_SEG, 3, 0));
        CHECK(step_is(&o[2], WF_EXP_STEP_SEG, 7, 0));

        /* 4. Фильтрация */
        wf_seg_reg_t reg4[] = {
            R(1,5,false,0,0), 
            R(2,0,true,0,0), 
            R(4,5,true,0,0),
            {0} /* valid=false по умолчанию в инициализации нулями, но нужно явно */
        };
        reg4[3].idx = 5; reg4[3].rows = 5; reg4[3].finalized = true; reg4[3].valid = false;
        
        ret = wf_exp_plan(reg4, 4, 0, 0, o, 16);
        CHECK(ret == 1);
        CHECK(step_is(&o[0], WF_EXP_STEP_SEG, 4, 0));

        /* 5. Текущая сессия, открытый сегмент в конце */
        wf_seg_reg_t reg5[] = {R(10,64,true,0,64), R(11,64,true,64,128), R(12,12,false,128,140)};
        ret = wf_exp_plan(reg5, 3, 0, 140, o, 16);
        CHECK(ret == 3);
        CHECK(step_is(&o[0], WF_EXP_STEP_SEG, 10, 0));
        CHECK(step_is(&o[1], WF_EXP_STEP_SEG, 11, 0));
        CHECK(step_is(&o[2], WF_EXP_STEP_RING, 128, 140));

        /* 6. Разрыв в середине (persist-off) */
        wf_seg_reg_t reg6[] = {R(20,64,true,0,64), R(21,64,true,100,164)};
        ret = wf_exp_plan(reg6, 2, 0, 170, o, 16);
        CHECK(ret == 4);
        CHECK(step_is(&o[0], WF_EXP_STEP_SEG, 20, 0));
        CHECK(step_is(&o[1], WF_EXP_STEP_RING, 64, 100));
        CHECK(step_is(&o[2], WF_EXP_STEP_SEG, 21, 0));
        CHECK(step_is(&o[3], WF_EXP_STEP_RING, 164, 170));

        /* 7. Кольцо начинается позже сегментов */
        wf_seg_reg_t reg7[] = {R(30,64,true,0,64), R(31,64,true,64,128)};
        ret = wf_exp_plan(reg7, 2, 200, 300, o, 16);
        CHECK(ret == 3);
        CHECK(step_is(&o[0], WF_EXP_STEP_SEG, 30, 0));
        CHECK(step_is(&o[1], WF_EXP_STEP_SEG, 31, 0));
        CHECK(step_is(&o[2], WF_EXP_STEP_RING, 200, 300));

        /* 8. Удаленный сегмент внутри окна */
        wf_seg_reg_t reg8[] = {R(41,64,true,64,128)};
        ret = wf_exp_plan(reg8, 1, 0, 128, o, 16);
        CHECK(ret == 2);
        CHECK(step_is(&o[0], WF_EXP_STEP_RING, 0, 64));
        CHECK(step_is(&o[1], WF_EXP_STEP_SEG, 41, 0));

        /* 9. Смешанные старые и текущие, несортированные */
        wf_seg_reg_t reg9[] = {R(52,10,true,5,15), R(3,9,true,0,0), R(51,5,true,0,5), R(2,9,true,0,0)};
        ret = wf_exp_plan(reg9, 4, 0, 20, o, 16);
        CHECK(ret == 5);
        CHECK(step_is(&o[0], WF_EXP_STEP_SEG, 2, 0));
        CHECK(step_is(&o[1], WF_EXP_STEP_SEG, 3, 0));
        CHECK(step_is(&o[2], WF_EXP_STEP_SEG, 51, 0));
        CHECK(step_is(&o[3], WF_EXP_STEP_SEG, 52, 0));
        CHECK(step_is(&o[4], WF_EXP_STEP_RING, 15, 20));

        /* 10. Сегмент пересекает r0 */
        wf_seg_reg_t reg10[] = {R(60,64,true,0,64)};
        ret = wf_exp_plan(reg10, 1, 40, 80, o, 16);
        CHECK(ret == 2);
        CHECK(step_is(&o[0], WF_EXP_STEP_SEG, 60, 0));
        CHECK(step_is(&o[1], WF_EXP_STEP_RING, 64, 80));

        /* 11. Кольцо никогда не пусто (если total совпадает с g1) */
        wf_seg_reg_t reg11[] = {R(70,64,true,0,64)};
        ret = wf_exp_plan(reg11, 1, 0, 64, o, 16);
        CHECK(ret == 1);
        CHECK(step_is(&o[0], WF_EXP_STEP_SEG, 70, 0));

        /* 12. total меньше g0 (кольцо обрезано) */
        wf_seg_reg_t reg12[] = {R(80,10,true,50,60)};
        ret = wf_exp_plan(reg12, 1, 0, 30, o, 16);
        CHECK(ret == 2);
        CHECK(step_is(&o[0], WF_EXP_STEP_RING, 0, 30));
        CHECK(step_is(&o[1], WF_EXP_STEP_SEG, 80, 0));

        /* 13. cap слишком мал */
        ret = wf_exp_plan(reg6, 2, 0, 170, o, 3);
        CHECK(ret == -1);
        
        ret = wf_exp_plan(reg6, 2, 0, 170, o, 4);
        CHECK(ret == 4);

        /* 14. Нет дубликатов строк (проверка суммы для кейса 6) */
        uint32_t total_rows = 0;
        for(int i=0; i<ret; ++i) {
            if(o[i].kind == WF_EXP_STEP_RING) {
                total_rows += (o[i].b - o[i].a);
            } else {
                /* Находим сегмент в reg6 */
                for(int j=0; j<2; ++j) {
                    if(reg6[j].idx == o[i].a) {
                        total_rows += (reg6[j].g1 - reg6[j].g0);
                        break;
                    }
                }
            }
        }
        CHECK(total_rows == 170);
    }
}
