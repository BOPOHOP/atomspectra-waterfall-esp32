#include "ota_busy.h"
#include "test_util.h"

static void busy_first_acquire_wins(void)
{
    ota_busy_owner_t st = OTA_BUSY_NONE;
    CHECK(ota_busy_try_acquire_pure(&st, OTA_BUSY_MANUAL));
    CHECK(st == OTA_BUSY_MANUAL);
}

static void busy_second_acquire_blocked_same_owner(void)
{
    ota_busy_owner_t st = OTA_BUSY_NONE;
    CHECK(ota_busy_try_acquire_pure(&st, OTA_BUSY_MANUAL));
    CHECK(!ota_busy_try_acquire_pure(&st, OTA_BUSY_MANUAL));
    CHECK(st == OTA_BUSY_MANUAL);
}

static void busy_second_acquire_blocked_other_owner(void)
{
    ota_busy_owner_t st = OTA_BUSY_NONE;
    CHECK(ota_busy_try_acquire_pure(&st, OTA_BUSY_MANUAL));
    CHECK(!ota_busy_try_acquire_pure(&st, OTA_BUSY_GITHUB));
    CHECK(st == OTA_BUSY_MANUAL);   // владелец не подменяется чужим неудачным acquire
}

static void busy_release_by_owner_frees_state(void)
{
    ota_busy_owner_t st = OTA_BUSY_NONE;
    CHECK(ota_busy_try_acquire_pure(&st, OTA_BUSY_GITHUB));
    ota_busy_release_pure(&st, OTA_BUSY_GITHUB);
    CHECK(st == OTA_BUSY_NONE);
    CHECK(ota_busy_try_acquire_pure(&st, OTA_BUSY_MANUAL));   // теперь свободно для другого
}

static void busy_release_by_wrong_owner_is_noop(void)
{
    ota_busy_owner_t st = OTA_BUSY_NONE;
    CHECK(ota_busy_try_acquire_pure(&st, OTA_BUSY_MANUAL));
    ota_busy_release_pure(&st, OTA_BUSY_GITHUB);   // чужой release -- не освобождает
    CHECK(st == OTA_BUSY_MANUAL);
    CHECK(!ota_busy_try_acquire_pure(&st, OTA_BUSY_GITHUB));
}

static void busy_release_when_free_is_noop(void)
{
    ota_busy_owner_t st = OTA_BUSY_NONE;
    ota_busy_release_pure(&st, OTA_BUSY_MANUAL);   // не должно падать/менять состояние
    CHECK(st == OTA_BUSY_NONE);
}

// sweep-B задача 2: предикат "занято ли КЕМ-ЛИБО" для автосейва, без
// побочного эффекта (в отличие от acquire).
static void busy_is_busy_false_when_free(void)
{
    CHECK(!ota_busy_is_busy_pure(OTA_BUSY_NONE));
}

static void busy_is_busy_true_for_either_owner(void)
{
    CHECK(ota_busy_is_busy_pure(OTA_BUSY_MANUAL));
    CHECK(ota_busy_is_busy_pure(OTA_BUSY_GITHUB));
}

// автосейв обязан увидеть занятость/свободу сразу после acquire/release.
static void busy_is_busy_reflects_acquire_release_cycle(void)
{
    ota_busy_owner_t st = OTA_BUSY_NONE;
    CHECK(!ota_busy_is_busy_pure(st));
    CHECK(ota_busy_try_acquire_pure(&st, OTA_BUSY_GITHUB));
    CHECK(ota_busy_is_busy_pure(st));
    ota_busy_release_pure(&st, OTA_BUSY_GITHUB);
    CHECK(!ota_busy_is_busy_pure(st));
}

void ota_busy_suite(void)
{
    busy_first_acquire_wins();
    busy_second_acquire_blocked_same_owner();
    busy_second_acquire_blocked_other_owner();
    busy_release_by_owner_frees_state();
    busy_release_by_wrong_owner_is_noop();
    busy_release_when_free_is_noop();
    busy_is_busy_false_when_free();
    busy_is_busy_true_for_either_owner();
    busy_is_busy_reflects_acquire_release_cycle();
}
