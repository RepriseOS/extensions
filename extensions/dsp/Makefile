# SPDX-License-Identifier: GPL-3.0-only
main_SOURCES := main.c limiter.c math.c filter.c
# The cascade multiplies 32 by 32 to 64 bits per coefficient and sample: one
# ARM instruction each, a library call each in Thumb.
main_ARM_SOURCES := cascade.c
include $(or $(SDK),$(error set SDK to an unpacked revola SDK))/rv.mk
