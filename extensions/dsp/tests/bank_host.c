/* SPDX-License-Identifier: GPL-3.0-only */

/* shared/bank.c as the tests drive it: one bank over the rv_api interrupt
 * masking that counts, built once for each BANK_STAGES the effects use. */

#include "bank.h"

static uint32_t irq_calls;

static uint32_t irq_save(void)
{
    irq_calls++;
    return 0;
}

static void irq_restore(uint32_t state)
{
    (void)state;
    irq_calls++;
}

uint32_t test_irq_calls(void)
{
    return irq_calls;
}

static const struct rv_api api = {
    .size = sizeof api, .irq_save = irq_save, .irq_restore = irq_restore};

static struct bank bank = {.effect.api = &api};

void test_publish(const struct bank_set *set)
{
    bank_publish(&bank, set);
}

void test_process(int16_t *samples, uint32_t frames, uint32_t rate)
{
    bank_process(&bank, samples, frames, rate);
}
