#!/usr/bin/env python3
"""Extract the C++ convoy catalog (src/entities/level_data.cc) into a
generated Rust module (rust/kernel/src/level_data.rs).

Why generated: the catalog is ~450 lines of hand-tuned spline controls and
convoy recipes. Transcribing it by hand would be slow and error-prone; the
golden corpus (level.* rows) already pins every value bit-exactly, so the
transcription only has to be MECHANICAL. This script is that mechanism.
"""
import re
import sys

SRC = "src/entities/level_data.cc"
DST = "rust/kernel/src/level_data.rs"

src = open(SRC).read()

# ---------------------------------------------------------------- FlightPath
# Only brace-initialised control-point splines are catalog paths. The two
# FlightPath::Loop factories use cos/sin and are referenced by no convoy
# table (dead vocabulary), so the regex below naturally excludes them.
paths = {}          # name -> [(x, y), ...] control points
path_order = []     # source declaration order
for m in re.finditer(r"const FlightPath (\w+)\(\{(.*?)\}\);", src, re.S):
    name, body = m.group(1), m.group(2)
    pts = re.findall(r"\{\s*(-?\d+(?:\.\d+)?)[fF]?\s*,\s*(-?\d+(?:\.\d+)?)[fF]?\s*\}", body)
    if not pts:
        sys.exit(f"path {name}: no control points parsed")
    paths[name] = pts
    path_order.append(name)

# ------------------------------------------------------------------ tables
ENTRY = re.compile(
    r"\{\s*(\d+),\s*TextureId::(\w+),\s*(\d+),\s*(?:&(\w+)|nullptr),\s*"
    r"(true|false),\s*(true|false)(?:,\s*(true|false),\s*(\d+))?\s*\}")
tables = {}          # name -> [entry dicts]
table_order = []
for m in re.finditer(r"const ConvoyData (\w+)\[\] = \{(.*?)\};", src, re.S):
    name, body = m.group(1), m.group(2)
    entries = []
    for e in ENTRY.finditer(body):
        tex = e.group(2)
        if tex == "None":
            break  # sentinel terminates the table
        entries.append({
            "wait": int(e.group(1)), "tex": tex, "pc": int(e.group(3)),
            "path": e.group(4), "mir": e.group(5) == "true",
            "split": e.group(6) == "true",
            "sim": (e.group(7) or "false") == "true",
            "dock": int(e.group(8) or 0),
        })
    if not entries:
        sys.exit(f"table {name}: no entries parsed")
    # cross-check: every entry carries exactly one TextureId:: reference, so
    # live entries + the sentinel must account for all of them. A mismatch
    # means the entry regex silently dropped a row.
    tex_refs = len(re.findall(r"TextureId::", body))
    if tex_refs != len(entries) + 1:
        sys.exit(f"table {name}: {tex_refs} TextureId refs but {len(entries)}+1 entries parsed")
    tables[name] = entries
    table_order.append(name)

# ------------------------------------------------- levels_stage1 / stage2
def level_list(var):
    m = re.search(rf"const ConvoyData\* const {var}\[\] = \{{(.*?)\}};", src, re.S)
    if not m:
        sys.exit(f"{var} not found")
    names = re.findall(r"(\w+)", m.group(1))
    for n in names:
        if n not in tables:
            sys.exit(f"{var} references unknown table {n}")
    return names

s1 = level_list("levels_stage1")
s2 = level_list("levels_stage2")
if len(s1) != 15 or len(s2) != 15:
    sys.exit(f"stage tables must have 15 entries each, got {len(s1)}/{len(s2)}")

# ------------------------------------------- registry: first-encounter order
registry = []        # path names in first-encounter order across tables
for tname in s1 + s2:
    for e in tables[tname]:
        if e["path"] is None:
            sys.exit(f"table {tname}: nullptr arrival inside live entries")
        if e["path"] not in paths:
            sys.exit(f"table {tname}: unknown path {e['path']}")
        if e["path"] not in registry:
            registry.append(e["path"])
unreferenced = [p for p in path_order if p not in registry]

def const_name(n):
    return re.sub(r"[^A-Z0-9]+", "_", n.upper())

def fmt(v):
    return v if ("." in v) else v + ".0"

# ------------------------------------------------------------------ emit
out = []
w = out.append
w("//! Port of src/entities/level_data.cc: the hand-authored convoy catalog.")
w("//!")
w("//! GENERATED FILE -- do not edit. Regenerate with the extraction script")
w("//! after any change to the C++ source (tools/extract_level.py).")
w("//!")
w("//! Two halves, mirroring the C++ contract exactly:")
w("//! - `paths()`: the arrival splines, resampled through the SAME golden-")
w("//!   pinned Catmull-Rom + arc-length resampler as production (path.rs).")
w("//!   The corpus `level.pt` rows pin every resampled point bit-exactly.")
w("//! - `get_convoy_data(level)`: the per-wave convoy tables (15 stage-1 +")
w("//!   15 stage-2), resolved with the production modulo/stage mapping.")
w("//!   The corpus `level.table` / `level.convoy` / `level.resolve` rows pin")
w("//!   every field. The C++ sentinel row (TextureId::None) is excluded --")
w("//!   Rust tables carry their length in the slice.")
w("//!")
w("//! The two `FlightPath::Loop` factories in the C++ file are referenced by")
w("//! no convoy table and are therefore not part of the catalog contract.")
w("")
w("use std::sync::OnceLock;")
w("")
w("use crate::fleet::ConvoySpec;")
w("use crate::path::{generate_smooth_spline, Vec2f};")
w("use crate::stage_catalog::{wave_to_stage, WAVES_PER_STAGE};")
w("use crate::textures::TextureId;")
w("")
w("// ------------------------------------------------ control-point splines")
for name in registry:
    pts = paths[name]
    w("")
    w(f"const {const_name(name)}_CTRL: &[Vec2f] = &[")
    line = "   "
    for x, y in pts:
        piece = f" Vec2f {{ x: {fmt(x)}, y: {fmt(y)} }},"
        if len(line) + len(piece) > 98:
            w(line)
            line = "   "
        line += piece
    if line.strip():
        w(line)
    w("];")
w("")
w("// ------------------------------------------------------- path registry")
for i, name in enumerate(registry):
    w(f"pub const PATH_{const_name(name)}: usize = {i};")
w("")
w("/// Resampled arrival splines, indexed by `ConvoySpec::path_id`.")
w("pub fn paths() -> &'static [Vec<Vec2f>] {")
w("    static PATHS: OnceLock<Vec<Vec<Vec2f>>> = OnceLock::new();")
w("    PATHS.get_or_init(|| {")
w("        vec![")
for name in registry:
    w(f"            generate_smooth_spline({const_name(name)}_CTRL),")
w("        ]")
w("    })")
w("}")
w("")
w("// -------------------------------------------------------- convoy tables")

def emit_table(cname, tname):
    w("")
    w(f"const {cname}: &[ConvoySpec] = &[")
    for e in tables[tname]:
        w("    ConvoySpec {")
        w(f"        wait: {e['wait']},")
        w(f"        texture_id: TextureId::{e['tex']},")
        w(f"        convoy_size_pc: {e['pc']},")
        w(f"        x_mirror: {str(e['mir']).lower()},")
        w(f"        split: {str(e['split']).lower()},")
        w(f"        simultaneous: {str(e['sim']).lower()},")
        w(f"        dock_align: {e['dock']},")
        w(f"        path_id: PATH_{const_name(e['path'])},")
        w("    },")
    w("];")

for tname in s1 + s2:
    emit_table(const_name(tname), tname)
w("")
w(f"static TABLES_S1: [&'static [ConvoySpec]; {len(s1)}] = [")
w("    " + ", ".join(const_name(t) for t in s1) + ",")
w("];")
w(f"static TABLES_S2: [&'static [ConvoySpec]; {len(s2)}] = [")
w("    " + ", ".join(const_name(t) for t in s2) + ",")
w("];")
w("")
w("/// LevelData::GetConvoyData -- `usize` mirrors the C++ `size_t` parameter")
w("/// (a negative level at a call site wraps identically in both languages).")
w("/// Level 0 clamps to wave 1; levels wrap every 15 waves; stage >= 2 uses")
w("/// the stage-2 tables.")
w("pub fn get_convoy_data(level: usize) -> &'static [ConvoySpec] {")
w("    let level = if level == 0 { 1 } else { level };")
w("    let idx = (level - 1) % WAVES_PER_STAGE as usize;")
w("    if wave_to_stage(level as i32) >= 2 {")
w("        TABLES_S2[idx]")
w("    } else {")
w("        TABLES_S1[idx]")
w("    }")
w("}")
w("")
w("/// LevelData::GetTotalLevels -- waves per stage cycle (15).")
w("pub fn get_total_levels() -> usize {")
w("    WAVES_PER_STAGE as usize")
w("}")
w("")

open(DST, "w").write("\n".join(out))

n_convoys = sum(len(tables[t]) for t in s1 + s2)
print(f"paths parsed:      {len(paths)} ({len(registry)} referenced, unreferenced: {unreferenced})")
print(f"tables:            {len(tables)} (s1={len(s1)}, s2={len(s2)})")
print(f"live convoys:      {n_convoys}")
print(f"ctrl points total: {sum(len(paths[p]) for p in registry)}")
print(f"written:           {DST} ({len(out)} lines)")
