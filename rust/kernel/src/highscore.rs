//! Port of src/highscore/highscore_table.{h,cc}: the Hall of Fame domain
//! model. Serialize() output is byte-identical to the C++ (golden vectors),
//! so both implementations read and write the same tamper-evident ledger.

use crate::mathx::Coord;
use crate::score_hash::line_digest;
use std::cmp::Ordering;
use std::collections::BTreeMap;

pub const MAX_PER_RESOLUTION: usize = 10;
pub const MAX_GLOBAL: usize = 100;

#[derive(Clone, Debug, Eq)]
pub struct HighScore {
    pub value: u64,
    pub name: String,
    pub date: i64,
    pub width: i32,
    pub height: i32,
    pub refresh: i32,
    pub stage: i32,
    pub wave: i32,
}

// C++ operator<: score ascending, then date ascending (full-field equality
// lives in the duplicate check below, never in the ordering).
impl Ord for HighScore {
    fn cmp(&self, other: &Self) -> Ordering {
        self.value.cmp(&other.value).then(self.date.cmp(&other.date))
    }
}
impl PartialOrd for HighScore {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}
impl PartialEq for HighScore {
    fn eq(&self, other: &Self) -> bool {
        self.cmp(other) == Ordering::Equal
    }
}

#[derive(Default)]
pub struct HighScoreTable {
    // BTreeMap key order == C++ std::map<Coord> order (lexicographic x,y).
    by_res: BTreeMap<(i32, i32), Vec<HighScore>>,
    all: Vec<HighScore>,
}

impl HighScoreTable {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn clear(&mut self) {
        self.by_res.clear();
        self.all.clear();
    }

    /// Same contract as C++ Add: rejection is reserved for exact duplicates
    /// (score, name, resolution, stage, wave AND date); distinct runs with
    /// identical scores are allowed.
    pub fn add(&mut self, hs: HighScore) -> bool {
        let dup = self.all.iter().any(|e| {
            e.value == hs.value
                && e.name == hs.name
                && e.width == hs.width
                && e.height == hs.height
                && e.stage == hs.stage
                && e.wave == hs.wave
                && e.date == hs.date
        });
        if dup {
            return false;
        }
        let bucket = self.by_res.entry((hs.width, hs.height)).or_default();
        let pos = bucket.binary_search_by(|e| e.cmp(&hs)).unwrap_or_else(|x| x);
        bucket.insert(pos, hs);
        self.prune_to_limit();
        true
    }

    /// Mirrors C++ PruneToLimit: per-resolution cap drops the lowest entries,
    /// then the global list is rebuilt from the survivors (value > 0) and
    /// capped. Entries equal under (value,date) keep insertion order -- the
    /// golden corpus has no such ties.
    pub fn prune_to_limit(&mut self) {
        for bucket in self.by_res.values_mut() {
            while bucket.len() > MAX_PER_RESOLUTION {
                bucket.remove(0);
            }
        }
        self.all.clear();
        for bucket in self.by_res.values() {
            for hs in bucket {
                if hs.value > 0 {
                    self.all.push(hs.clone());
                }
            }
        }
        self.all.sort();
        while self.all.len() > MAX_GLOBAL {
            self.all.remove(0);
        }
    }

    /// Exact twin of HighScoreTable::Serialize: header row, then descending
    /// (value, date), each row signed with the salted 64-bit digest.
    pub fn serialize_lines(&self) -> Vec<String> {
        let mut out = Vec::new();
        out.push("STAGE;WAVE;SCORE;WIDTH;HEIGHT;REFRESH_RATE;DATE;NAME;HASH".to_string());
        for hs in self.all.iter().rev() {
            if hs.value == 0 {
                continue;
            }
            let payload = format!(
                "{};{};{};{};{};{};{};{}",
                hs.stage, hs.wave, hs.value, hs.width, hs.height, hs.refresh, hs.date, hs.name
            );
            out.push(format!("{payload};{}", line_digest(&payload)));
        }
        out
    }

    pub fn all(&self) -> &[HighScore] {
        &self.all
    }

    /// Byte twin of `Serialize(ostream)`: every `serialize_lines` entry plus
    /// its trailing '\n' -- exactly the content SaveAsync would persist, and
    /// exactly what the corpus hashes with `content_hash`.
    pub fn serialized_content(&self) -> String {
        let mut out = String::new();
        for line in self.serialize_lines() {
            out.push_str(&line);
            out.push('\n');
        }
        out
    }
}

/// FNV-1a 64 over the serialized ledger (the dumper's `ContentHash64`): one
/// integer that pins the exact byte stream a `serialized_content` produces.
pub fn content_hash(content: &str) -> u64 {
    let mut hash: u64 = 14_695_981_039_346_656_037;
    for byte in content.as_bytes() {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(1_099_511_628_211);
    }
    hash
}

/// Twin of the `HighScores` pending ledger (src/highscore/highscore.h): the
/// "score awaiting entry" state that `CombatSession::CommitFinalScore` fills
/// and `HighScores::Update` consumes. Update's decision core lives here
/// (`consume_pending`); its I/O half -- Load on first call, SaveAsync after
/// an accepted entry, the worker thread -- is shell territory and stays in
/// C++ until the Rust shell exists.
#[derive(Clone)]
pub struct HighScores {
    has_pending: bool,
    last_value: u64,
    last_size: Coord,
    last_rate: i32,
    last_stage: i32,
    last_wave: i32,
}

impl Default for HighScores {
    fn default() -> Self {
        // C++ member initializers: size {1280,720}, rate 60, the rest zero.
        Self {
            has_pending: false,
            last_value: 0,
            last_size: Coord::new(1280, 720),
            last_rate: 60,
            last_stage: 0,
            last_wave: 0,
        }
    }
}

impl HighScores {
    pub fn new() -> Self {
        Self::default()
    }

    /// Twin of `HighScores::Add`: a zero score is a guarded no-op (an empty
    /// run never arms the ledger), anything else overwrites every field.
    pub fn add(&mut self, score: u64, window_size: Coord, refresh_rate: i32, stage: i32, wave: i32) {
        if score == 0 {
            return;
        }
        self.has_pending = true;
        self.last_value = score;
        self.last_size = window_size;
        self.last_rate = refresh_rate;
        self.last_stage = stage;
        self.last_wave = wave;
    }

    /// Twin of `CancelPending`: clears the flag and the value only -- the
    /// resolution/rate/stage/wave keep their last-run values (the corpus
    /// hs.state rows pin exactly that residue).
    pub fn cancel_pending(&mut self) {
        self.has_pending = false;
        self.last_value = 0;
    }

    /// The `HighScores::Update` decision core minus the I/O: build the entry
    /// from the pending state, feed the table, then clear the pending flag
    /// and value unconditionally (Update does this whether or not the table
    /// accepted the entry). Returns the entry and the table's verdict --
    /// `added` is false only for an exact all-field duplicate -- or `None`
    /// when nothing was consumable. `name` and `date` are the shell inputs
    /// Update would read (player name config, `std::time(nullptr)`).
    pub fn consume_pending(
        &mut self,
        table: &mut HighScoreTable,
        name: &str,
        date: i64,
    ) -> Option<(HighScore, bool)> {
        if !(self.has_pending && self.last_value > 0) {
            return None;
        }
        let entry = HighScore {
            value: self.last_value,
            name: name.to_string(),
            date,
            width: self.last_size.x,
            height: self.last_size.y,
            refresh: self.last_rate,
            stage: self.last_stage,
            wave: self.last_wave,
        };
        let added = table.add(entry.clone());
        self.cancel_pending();
        Some((entry, added))
    }

    pub fn has_pending(&self) -> bool {
        self.has_pending
    }
    pub fn last_value(&self) -> u64 {
        self.last_value
    }
    pub fn last_size(&self) -> Coord {
        self.last_size
    }
    pub fn last_rate(&self) -> i32 {
        self.last_rate
    }
    pub fn last_stage(&self) -> i32 {
        self.last_stage
    }
    pub fn last_wave(&self) -> i32 {
        self.last_wave
    }
}
