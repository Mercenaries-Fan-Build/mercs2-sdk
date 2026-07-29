#!/usr/bin/env python3
"""Generate m2/load_ladder.gen.h from loadprobe's phases.rs LADDER.

loadprobe is the single source of truth for the world-load milestone ladder. This script parses its `phases.rs` and emits a C
header so the runtime trigger facility (m2_loadtrigger) matches the *exact* same
substrings — no hand-kept duplicate, no drift.

Usage:
    python3 gen_ladder.py [PATH_TO_phases.rs]

If PATH_TO_phases.rs is omitted, the known checkout layouts are searched in order
(overridable with the LOADPROBE_PHASES_RS environment variable).

Run `make ladder` to regenerate, and `make ladder-check` to verify the committed
header is in sync with phases.rs (the drift guard CI runs).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "m2", "load_ladder.gen.h")
OUT_C = os.path.join(HERE, "m2", "load_ladder.gen.c")

# Where loadprobe might be, relative to this file.
#
# This SDK is consumed two ways and the sibling checkout sits at a different depth in each: as its
# own repo (HERE is the repo root) and as a submodule at <consumer>/sdk (HERE is one level down).
# A single hardcoded `../..` was right for exactly one of them, so both are tried, newest layout
# first. `mercs2-wad-simulator` now owns loadprobe; `mercenaries-game/tools/wad_simulator` is an
# older vendored copy kept in the list so an existing checkout keeps working.
_CANDIDATE_PHASES = [
    ("..", "mercs2-wad-simulator", "crates", "loadprobe", "src", "phases.rs"),
    ("..", "..", "mercs2-wad-simulator", "crates", "loadprobe", "src", "phases.rs"),
    ("..", "mercenaries-game", "tools", "wad_simulator",
     "crates", "loadprobe", "src", "phases.rs"),
    ("..", "..", "mercenaries-game", "tools", "wad_simulator",
     "crates", "loadprobe", "src", "phases.rs"),
]


def default_phases():
    """The first candidate that exists, or the first candidate so the error names something real."""
    tried = []
    for parts in _CANDIDATE_PHASES:
        p = os.path.normpath(os.path.join(HERE, *parts))
        if os.path.isfile(p):
            return p
        tried.append(p)
    return tried[0]

# Phase { idx: 0, name: "Process init", matches: &["a", "b"] },
PHASE_RE = re.compile(
    r'Phase\s*\{\s*idx:\s*(\d+)\s*,\s*'
    r'name:\s*"((?:[^"\\]|\\.)*)"\s*,\s*'
    r'matches:\s*&\[(.*?)\]\s*\}',
    re.DOTALL,
)
# string literals inside a matches: &[ ... ] list
STR_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
CONST_RE = lambda name: re.compile(r'pub\s+const\s+' + name + r'\s*:\s*usize\s*=\s*(\d+)\s*;')


def c_escape(s: str) -> str:
    # Rust string-literal escapes we care about map 1:1 to C.
    return s.replace('\\', '\\\\').replace('"', '\\"')


def parse(text: str):
    phases = []
    for m in PHASE_RE.finditer(text):
        idx = int(m.group(1))
        name = m.group(2)
        matches = STR_RE.findall(m.group(3))
        if not matches:
            sys.exit(f"error: phase idx {idx} ({name!r}) has no match substrings")
        phases.append((idx, name, matches))
    if not phases:
        sys.exit("error: no Phase entries found in phases.rs (format changed?)")
    phases.sort(key=lambda p: p[0])
    for i, (idx, _, _) in enumerate(phases):
        if idx != i:
            sys.exit(f"error: ladder idx not contiguous: expected {i}, got {idx}")

    def const(name, default):
        mm = CONST_RE(name).search(text)
        return int(mm.group(1)) if mm else default

    reached = const("REACHED_WORLD_IDX", phases[-1][0])
    entered = const("ENTERED_WORLD_IDX", 0)
    return phases, reached, entered


def emit(phases, reached, entered, _src_path: str) -> str:
    # Provenance names the FILE, never the path it was found at. `--check` diffs the whole emitted
    # text, so encoding a machine-specific directory layout makes the guard fire whenever the script
    # is run from a different checkout — reporting drift when the ladder is byte-identical. The
    # guard has to answer "did the ladder change", not "where was it run from".
    out = []
    out.append("/* AUTO-GENERATED from loadprobe by gen_ladder.py — DO NOT EDIT. */")
    out.append("/* Source of truth: loadprobe/src/phases.rs */")
    out.append("#ifndef M2_LOAD_LADDER_GEN_H")
    out.append("#define M2_LOAD_LADDER_GEN_H")
    out.append("")
    out.append(f"#define M2_LADDER_COUNT {len(phases)}")
    out.append(f"#define M2_PHASE_REACHED_WORLD_IDX {reached}")
    out.append(f"#define M2_PHASE_ENTERED_WORLD_IDX {entered}")
    out.append("")
    out.append("typedef struct {")
    out.append("    int idx;")
    out.append("    const char* name;")
    out.append("    const char* const* matches;")
    out.append("    int match_count;")
    out.append("} M2LoadPhase;")
    out.append("")
    out.append("/* The table itself lives in the DLL (load_ladder.gen.c), not here. Defined in a")
    out.append(" * header it would be duplicated into every consumer's translation unit — and a")
    out.append(" * `static` array cannot be exported, so the shared layer could not own it. Mods use")
    out.append(" * the M2_PHASE_* constants above; only m2_loadtrigger reads the table. */")
    out.append("extern const M2LoadPhase k_m2_ladder[M2_LADDER_COUNT];")
    out.append("")
    out.append("#endif /* M2_LOAD_LADDER_GEN_H */")
    out.append("")
    return "\n".join(out)


def emit_source(phases) -> str:
    """The ladder table, compiled into the DLL exactly once."""
    out = []
    out.append("/* AUTO-GENERATED from loadprobe by gen_ladder.py — DO NOT EDIT. */")
    out.append("/* Source of truth: loadprobe/src/phases.rs */")
    out.append('#include "load_ladder.gen.h"')
    out.append("")
    for idx, _name, matches in phases:
        lits = ", ".join(f'"{c_escape(s)}"' for s in matches)
        out.append(f"static const char* const k_m2_phase_{idx}_matches[] = {{ {lits} }};")
    out.append("")
    out.append("const M2LoadPhase k_m2_ladder[M2_LADDER_COUNT] = {")
    for idx, name, matches in phases:
        out.append(
            f'    {{ {idx}, "{c_escape(name)}", '
            f"k_m2_phase_{idx}_matches, {len(matches)} }},"
        )
    out.append("};")
    out.append("")
    return "\n".join(out)


def main():
    positional = [a for a in sys.argv[1:] if not a.startswith("--")]
    check = "--check" in sys.argv
    src = positional[0] if positional else os.environ.get(
        "LOADPROBE_PHASES_RS", default_phases())
    src = os.path.abspath(src)
    if not os.path.exists(src):
        sys.exit(f"error: phases.rs not found at {src}\n"
                 f"       pass the path explicitly or set LOADPROBE_PHASES_RS")
    with open(src, encoding="utf-8") as f:
        text = f.read()
    phases, reached, entered = parse(text)
    # Two outputs: the header a consumer sees, and the table the DLL compiles. BOTH are checked —
    # guarding only one would let the table drift while the constants stayed put, which is the
    # failure mode that matters (m2_loadtrigger silently stops matching a renamed milestone).
    wanted = {OUT: emit(phases, reached, entered, src), OUT_C: emit_source(phases)}

    if check:
        for path, text_wanted in wanted.items():
            if not os.path.exists(path):
                sys.exit(f"DRIFT: {os.path.basename(path)} does not exist; run `make ladder`")
            with open(path, encoding="utf-8") as f:
                if f.read() != text_wanted:
                    sys.exit(f"DRIFT: {os.path.basename(path)} is out of sync with "
                             f"{src}; run `make ladder`")
        print(f"ladder in sync ({len(phases)} phases)")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    for path, text_wanted in wanted.items():
        with open(path, "w", encoding="utf-8") as f:
            f.write(text_wanted)
    print(f"wrote {os.path.basename(OUT)} + {os.path.basename(OUT_C)} "
          f"({len(phases)} phases, reached={reached}, entered={entered}) from {src}")


if __name__ == "__main__":
    main()
