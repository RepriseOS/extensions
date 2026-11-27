# Fast Seek

The same approach as [Seek MP4 audio by chunk instead of sample by sample](https://github.com/RepriseOS/repriseos/pull/63) by kayboll for RepriseOS, arrived at independently.

Apple's MP4 parser seeks an AAC or Apple Lossless file (M4A, M4B) by stepping through every sample to the target, reading the sample-size table from disk in 128 KB windows. A backward seek starts from the first sample. A jump of several hours in an audiobook takes seconds and many disk reads. Chapters, the scrubber and resuming an episode all seek this way.

The extension binds the entry of the sample walk. It finds the chunk holding the target from the in-memory time-to-sample and sample-to-chunk tables and starts the walk at that chunk's first sample, leaving Apple's loop at most a few seconds of audio in iTunes files. Files with more than one edit, or more than one frame length before the target, seek the stock way.

Disabling it restores Apple's walk at once.

| | |
|---|---|
| Default | on |
| Memory | image only |
| Settings | none |
