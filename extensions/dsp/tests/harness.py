# SPDX-License-Identifier: GPL-3.0-only
"""What the host test suites share besides the DSP model: the ctypes build, the
noise they feed the effects and the readers of the label strings."""

import ctypes
import subprocess

FLAGS = [
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-g",
    "-fno-sanitize-recover=all",
    "-D_FORTIFY_SOURCE=0",  # macOS string.h would wrap what rv_libc.h declares
]


def build(work, name, sources, includes, *flags):
    """A shared object for ctypes; undefined behaviour checks only, since the
    address sanitizer cannot load through dlopen."""
    library = work / name
    subprocess.run(
        ["cc", *FLAGS, "-fsanitize=undefined", "-shared", "-fPIC"]
        + [f"-I{path}" for path in includes]
        + list(flags)
        + [str(source) for source in sources]
        + ["-o", str(library)],
        check=True,
    )
    return ctypes.CDLL(str(library))


def noise(count, seed):
    """Independent noise on each channel, left then right from one LCG."""
    value = seed
    frames = []
    for _ in range(count):
        pair = []
        for _ in range(2):
            value = (value * 1664525 + 1013904223) & 0xFFFFFFFF
            sample = value >> 16
            pair.append(sample - 65536 if sample >= 32768 else sample)
        frames.append(tuple(pair))
    return frames


def strings(dll, name, count):
    """The `count` strings of the const char * array `name`."""
    return [s.decode() for s in (ctypes.c_char_p * count).in_dll(dll, name)]


def labeller(function):
    """labels(parameter, count) over a `<name>_label(parameter, index, out)`."""
    function.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
    function.restype = ctypes.c_char_p
    out = ctypes.create_string_buffer(16)

    def labels(parameter, count):
        return [function(parameter, i, out).decode() for i in range(count)]

    return labels
