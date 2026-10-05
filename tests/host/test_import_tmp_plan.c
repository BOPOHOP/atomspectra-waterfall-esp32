#include "../../main/import_tmp_plan.h"
#include <stdio.h>
#include <stddef.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void) {
    CHECK(import_tmp_is_orphan("import.tmp") == true);

    CHECK(import_tmp_is_orphan("saved.html") == false);
    CHECK(import_tmp_is_orphan("spec_0001.bin") == false);
    CHECK(import_tmp_is_orphan("spec_0012.bin") == false);
    CHECK(import_tmp_is_orphan("spec_0001.bin.tmp") == false);
    CHECK(import_tmp_is_orphan("bk_3_12.bin.tmp") == false);
    CHECK(import_tmp_is_orphan("bk_3_12.bin") == false);
    CHECK(import_tmp_is_orphan("import.tmp.bak") == false);
    CHECK(import_tmp_is_orphan("import.tmpx") == false);
    CHECK(import_tmp_is_orphan("xmport.tmp") == false);
    CHECK(import_tmp_is_orphan("Import.tmp") == false);
    CHECK(import_tmp_is_orphan("IMPORT.TMP") == false);
    CHECK(import_tmp_is_orphan("import.tm") == false);
    CHECK(import_tmp_is_orphan("import_tmp") == false);
    CHECK(import_tmp_is_orphan("mport.tmp") == false);
    CHECK(import_tmp_is_orphan("import.tmp ") == false);
    CHECK(import_tmp_is_orphan(" import.tmp") == false);
    CHECK(import_tmp_is_orphan("") == false);
    CHECK(import_tmp_is_orphan(".") == false);
    CHECK(import_tmp_is_orphan("..") == false);
    CHECK(import_tmp_is_orphan("current.bin.tmp") == false);
    CHECK(import_tmp_is_orphan("base.bin.tmp") == false);
    CHECK(import_tmp_is_orphan("wf_ref.bin.tmp") == false);

    CHECK(import_tmp_is_orphan(NULL) == false);

    if (fails == 0) {
        printf("test_import_tmp_plan: OK\n");
        return 0;
    } else {
        printf("test_import_tmp_plan: FAILED\n");
        return 1;
    }
}
