# Long Audio Fix

The same fix as [Play MP4 audiobooks past 13.5 hours](https://github.com/RepriseOS/repriseos/pull/62) by kayboll for RepriseOS, arrived at independently.

Apple's MP4 parser ends an AAC or Apple Lossless file (M4A, M4B) once playback passes 2^31 samples: 13:31:35 at 44.1 kHz, 12:25:39 at 48 kHz. Past that point every sample counts as outside the edit list, so the track ends, and a seek there ends it at once. This affects files with an edit list, which iTunes, ffmpeg and most audiobook tools write.

The parser keeps the decode time in 64 bits but compares it with the edit list in 32. The extension replaces the three sign-extensions of the low word with loads of the high word.

Disabling it restores Apple's compares at once.

| | |
|---|---|
| Default | on |
| Memory | image only |
| Settings | none |
