/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BACKLIGHT_PROBE_HW_H
#define BACKLIGHT_PROBE_HW_H
#include <stdint.h>

/* Apple OSOS 2.0.5 PMU access, found by disassembling the running firmware:
 *   0x08363dec  int pmu_read(reg, count, buf)    I2C 0x73 (0xE6 >> 1)
 *   0x08363f58  int pmu_write(reg, count, buf)   returns 0 on success
 *   0x08363e78  backlight_set_level(level): clamps to [6, *0x089d3514], caches
 *               the level at 0x089d3515 and writes PMU register 0x28.
 * Apple brackets PMU access with locks 17 and 5 (0x0805f67c, 0x0805f66c) and
 * their releases (0x0805970c, 0x08059768); so does this module.
 * The stock floor is the `cmp r0, #6 / movcc r0, #6` at 0x08363e7c. */

enum { STOCK_FLOOR = 6 };

/* 1 when every Apple function above holds the instruction words this module
 * was written against; nothing touches the PMU otherwise. */
int hw_check(void);
int hw_read(uint8_t reg, uint8_t *out);
int hw_write(uint8_t reg, uint8_t value);
/* Apple's level cache and ceiling. */
int hw_cached_level(void);
void hw_set_cached_level(uint8_t level);
int hw_stock_max(void);

#endif
