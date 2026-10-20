"""Make ESP32-audioI2S link with GCC 14.

The library declares an abstract `Decoder` base class whose only non-pure virtual is a
*defaulted, inline* destructor:

    virtual ~Decoder() = default;

A class like that has no "key function", so GCC has nowhere to anchor the vtable and
nothing emits one - the link then fails with:

    undefined reference to `_ZTV7Decoder'

(Verified: `nm` shows `U _ZTV7Decoder` in every object of the archive, never a
definition. It happens on both 3.4.7 and 4.0.0, so pinning a version does not help.)

The fix is the textbook one: give the destructor an out-of-line definition in the
library's own translation unit, which becomes the key function and makes Audio.cpp the
place the vtable is emitted.

This runs on every build, is idempotent, and only touches the vendored copy under
.pio/libdeps - so it is re-applied automatically after a library update instead of
being a silent manual edit that a `pio pkg update` would wipe.
"""
Import("env")  # noqa: F821 - provided by PlatformIO's SCons environment
from pathlib import Path

HEADER_DECL = "virtual ~Decoder() = default;"
HEADER_FIXED = "virtual ~Decoder();"
CPP_MARKER = "Decoder::~Decoder() = default;"


def patch() -> None:
    libdir = (Path(env.subst("$PROJECT_LIBDEPS_DIR"))  # noqa: F821
              / env.subst("$PIOENV")                    # noqa: F821
              / "ESP32-audioI2S" / "src")
    header, source = libdir / "Audio.h", libdir / "Audio.cpp"
    if not header.is_file() or not source.is_file():
        print("[patch-audio] library not present yet, skipping")
        return

    text = header.read_text(encoding="utf-8", errors="replace")
    if HEADER_DECL in text:
        header.write_text(text.replace(HEADER_DECL, HEADER_FIXED, 1), encoding="utf-8")
        print("[patch-audio] Audio.h: Decoder destructor is now out-of-line")
    elif HEADER_FIXED not in text:
        print("[patch-audio] WARNING: expected declaration not found; library changed?")

    body = source.read_text(encoding="utf-8", errors="replace")
    extra = ""
    if CPP_MARKER not in body:
        extra += ("\n// Added by esp32/scripts/patch_audio_lib.py:\n"
                  "// the out-of-line destructor is the key function that makes GCC\n"
                  "// emit Decoder's vtable (otherwise the link fails on _ZTV7Decoder).\n"
                  "Decoder::~Decoder() = default;\n")
    # Two more virtuals are declared without '= 0' and never defined anywhere, so the
    # vtable cannot be completed. They exist for ICY/web-stream metadata, which a local
    # file never produces - empty strings are the honest value here, and safer than
    # nullptr because callers print them.
    if "Decoder::getStreamTitle" not in body:
        extra += ("\n// Added by esp32/scripts/patch_audio_lib.py: declared-but-undefined\n"
                  "// virtuals that the vtable needs. Unused for local file playback.\n"
                  "const char* Decoder::getStreamTitle() { return \"\"; }\n"
                  "const char* Decoder::whoIsIt()        { return \"decoder\"; }\n")
    if extra:
        source.write_text(body + extra, encoding="utf-8")
        print("[patch-audio] Audio.cpp: appended missing Decoder definitions")


patch()
