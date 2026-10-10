#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Строит fixtures/stand_capture.h из свежего захвата стенда (ESP32\\data\\spectrum_<UTC>.{meta.json,bin}).
Образцы боевого вида (#SA-3a): серийник, калибровка, счётчики и bins реального прибора, не выдуманные.
Запуск: PYTHONIOENCODING=utf-8 python gen_stand_fixture.py <каталог захвата> <метка UTC> <выход.h>"""
import json, struct, sys, os
d, tag, out = sys.argv[1], sys.argv[2], sys.argv[3]
raw = open(os.path.join(d, f"spectrum_{tag}.meta.json"), encoding="utf-8").read()
m = json.loads(raw)
bins = struct.unpack("<8192I", open(os.path.join(d, f"spectrum_{tag}.bin"), "rb").read())
f = open(out, "w", encoding="utf-8", newline="\n")
f.write(f"// Захват стенда {tag} (ESP32/data/spectrum_{tag}.*), живой прибор. Не править: fixtures/gen_stand_fixture.py.\n")
f.write("#pragma once\n#include <stdint.h>\n")
f.write(f'#define STAND_SERIAL "{m["serial"]}"\n')
for k in ("total", "cpu", "cps", "lost", "time", "bridge_drop", "usb_rx_err", "rx_ring_drops", "hist_ok", "hist_drop", "autosave_age_sec", "autosave_fail_streak", "dead", "reset_unconfirmed"):
    f.write(f"#define STAND_{k.upper()} {m[k]}u\n")
f.write(f"#define STAND_LIVE {m['live']!r}\n#define STAND_T1 {m['t1']!r}\n")
f.write("static const double STAND_CALIB[5] = {" + ", ".join(repr(x) for x in m["calib"]) + "};\n")
f.write("static const char STAND_META_JSON[] = " + json.dumps(raw) + ";\n")
f.write("static const uint32_t STAND_BINS[8192] = {" + ",".join(map(str, bins)) + "};\n")
