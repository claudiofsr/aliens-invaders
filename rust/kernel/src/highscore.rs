//! Port of src/highscore/highscore_table.{h,cc}: the Hall of Fame domain
//! model. Serialize() output is byte-identical to the C++ (golden vectors),
//! so both implementations read and write the same tamper-evident ledger.

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
}
