# Night Light (RepriseOS extension, test build 0.2.0)

Lowers the iPod Classic backlight below Apple's floor (level 6 of the PMU's
1..63) to a chosen level 1-5, with optional extra gamma dimming, always or
in a daily window that fades in/out over 10 minutes.

Settings > Night Light: Night Light (Off/On/Scheduled), Backlight (Stock, 5..1),
Extra Dimming (Off, 10..70 %), From / Until.

Backlight writes use Apple's own pmu_write (0x08363f58) and locks, after
checking 11 firmware instruction words (hw.c); mismatched firmware leaves the
backlight alone. Apple's cached level is not changed, and turning Night Light
off writes it back. Don't run alongside Night Shift (both own the gamma tables).

Build: from the repository root, `make night-light` (see catalog/README.md for the SDK and toolchain).
Test: `make check` runs test.c on the host.
