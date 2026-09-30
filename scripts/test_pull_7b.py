#!/usr/bin/env python3
"""#DATA-7b: pick_dst_7b не затирает и не теряет сегмент с тем же именем и размером, но другим содержимым."""
import os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wf_pull_client import pick_dst_7b

with tempfile.TemporaryDirectory() as d:
    dst = os.path.join(d, "seg_00001.aswf")
    a, b, c = b"A" * 64, b"B" * 64, b"C" * 64
    assert pick_dst_7b(dst, a) == dst                      # файла нет -> пишем сюда
    open(dst, "wb").write(a)
    assert pick_dst_7b(dst, a) is None                     # те же байты -> повтор, писать не надо
    assert pick_dst_7b(dst, b) == dst + ".dup1"            # то же имя и размер, другое содержимое -> .dup1
    open(dst + ".dup1", "wb").write(b)
    assert pick_dst_7b(dst, b) is None                     # уже лежит как .dup1
    assert pick_dst_7b(dst, c) == dst + ".dup2"            # третий вариант -> .dup2
    assert open(dst, "rb").read() == a                     # оригинал не тронут
print("test_pull_7b: OK")
