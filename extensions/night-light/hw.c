/* SPDX-License-Identifier: GPL-3.0-only */
#include "hw.h"
#include <stddef.h>

typedef int (*pmu_read_fn)(uint32_t reg, uint32_t count, void *buf);
typedef int (*pmu_write_fn)(uint32_t reg, uint32_t count, const void *buf);
typedef void (*lock_fn)(void);

#define PMU_READ ((pmu_read_fn)0x08363dec)
#define PMU_WRITE ((pmu_write_fn)0x08363f58)
#define LOCK_A ((lock_fn)0x0805f67c)   /* lock 17 */
#define LOCK_B ((lock_fn)0x0805f66c)   /* lock 5 */
#define UNLOCK_B ((lock_fn)0x0805970c) /* release 5 */
#define UNLOCK_A ((lock_fn)0x08059768) /* release 17 */
#define LEVEL_MAX_BYTE ((volatile uint8_t *)0x089d3514)
#define LEVEL_CACHE ((volatile uint8_t *)0x089d3515)

static const struct {
    uint32_t address, word;
} expected[] = {
    {0x08363dec, 0xe92d40f8}, /* pmu_read: push {r3-r7, lr} */
    {0x08363dfc, 0xe3a00073}, /*   mov r0, #0x73 (PMU I2C address) */
    {0x08363f58, 0xe92d401c}, /* pmu_write: push {r2-r4, lr} */
    {0x08363f88, 0xe3a00073}, /*   mov r0, #0x73 */
    {0x08363e7c, 0xe3500006}, /* set_level: cmp r0, #6 */
    {0x08363eb0, 0x089d3514}, /*   literal: ceiling byte */
    {0x08363eb4, 0x089d3515}, /*   literal: level cache */
    {0x0805f67c, 0xe3a00011}, /* lock 17 */
    {0x0805f66c, 0xe3a00005}, /* lock 5 */
    {0x0805970c, 0xe3a00005}, /* release 5 */
    {0x08059768, 0xe3a00011}, /* release 17 */
};

static int ok = -1;

int hw_check(void)
{
    if (ok < 0) {
        ok = 1;
        for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++)
            if (*(const volatile uint32_t *)expected[i].address != expected[i].word)
                ok = 0;
    }
    return ok;
}

int hw_read(uint8_t reg, uint8_t *out)
{
    if (!hw_check())
        return -1;
    LOCK_A();
    LOCK_B();
    int status = PMU_READ(reg, 1, out);
    UNLOCK_B();
    UNLOCK_A();
    return status;
}

int hw_write(uint8_t reg, uint8_t value)
{
    if (!hw_check())
        return -1;
    uint8_t byte = value;
    LOCK_A();
    LOCK_B();
    int status = PMU_WRITE(reg, 1, &byte);
    UNLOCK_B();
    UNLOCK_A();
    return status;
}

int hw_cached_level(void)
{
    return hw_check() ? *LEVEL_CACHE : -1;
}

void hw_set_cached_level(uint8_t level)
{
    if (hw_check())
        *LEVEL_CACHE = level;
}

int hw_stock_max(void)
{
    return hw_check() ? *LEVEL_MAX_BYTE : -1;
}
