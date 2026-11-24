# SPDX-License-Identifier: GPL-3.0-only
main_SOURCES := main.c design.c modes.c
# Every sample is three or four 32 by 32 to 64 bit multiplies: one smull each
# in ARM, a library call each in Thumb.
main_ARM_SOURCES := crossfeed.c
editor_SOURCES := editor.c response.c labels.c modes.c
EXTRA_CFLAGS := -I../shared
include $(or $(SDK),$(error set SDK to an unpacked revola SDK))/rv.mk
