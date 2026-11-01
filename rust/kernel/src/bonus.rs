//! Port of the supply-pod half of src/entities/managers.{h,cc} plus the
//! Bonus type vocabulary (src/entities/actors.h): BonusSlot, BonusManager and
//! the Bonus -> TextureId mapping. Pool layout and swap-pop order are
//! observation-load-bearing for the golden vectors, exactly like bullets.rs.

use crate::game_object::GameObject;
use crate::mathx::Coord;
use crate::textures::{dims, TextureId};
use crate::trajectory::ViewportMetrics;

/// GameRules::Combat::kMaxBonuses
const MAX_BONUSES: usize = 64;

/// GameRules::Player::kCollisionHitboxScale
const COLLISION_HITBOX_SCALE: f32 = 0.65;

/// Twin of Bonus::bonus_t (actors.h): the shared bonus-type vocabulary.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum BonusType {
    None = 0,
    ExtraSpeed = 1,
    ExtraFire = 2,
    ExtraShield = 3,
    ExtraMulti = 4,
    ExtraNuke = 5,
}

/// Twin of GetBonusTextureId (actors.h, the SSOT mapping).
pub fn bonus_texture_id(t: BonusType) -> TextureId {
    match t {
        BonusType::ExtraSpeed => TextureId::BonusSpeed,
        BonusType::ExtraFire => TextureId::BonusFire,
        BonusType::ExtraShield => TextureId::BonusShield,
        BonusType::ExtraMulti => TextureId::BonusMulti,
        BonusType::ExtraNuke => TextureId::BonusNuke,
        BonusType::None => TextureId::None,
    }
}

/// One pooled supply pod (BonusSlot twin). Speed is a constant fall: the C++
/// default member initializer Coord(0, 1) re-armed on every Add.
#[derive(Clone, Copy)]
pub struct BonusSlot {
    pub tex: TextureId,
    pub pos: Coord,
    pub speed: Coord,
    pub bonus_type: BonusType,
    pub active: bool,
}

impl Default for BonusSlot {
    fn default() -> Self {
        Self {
            tex: TextureId::None,
            pos: Coord::new(0, 0),
            speed: Coord::new(0, 1),
            bonus_type: BonusType::None,
            active: false,
        }
    }
}

impl BonusSlot {
    fn out(&self, win_h: i32) -> bool {
        let (_w, h) = dims(self.tex);
        let hh = h / 2;
        self.pos.y - hh >= win_h
    }

    fn collide(&self, other: &GameObject) -> bool {
        let (w, h) = dims(self.tex);
        let hh1 = (h as f32 * COLLISION_HITBOX_SCALE) as i32;
        let hh2 = (other.height() as f32 * COLLISION_HITBOX_SCALE) as i32;
        let dy = (self.pos.y - other.pos.y).abs();
        if dy * 2 >= hh1 + hh2 {
            return false;
        }
        let hw1 = (w as f32 * COLLISION_HITBOX_SCALE) as i32;
        let hw2 = (other.width() as f32 * COLLISION_HITBOX_SCALE) as i32;
        (self.pos.x - other.pos.x).abs() * 2 < hw1 + hw2
    }
}

/// BonusManager twin: dense active-index pool with swap-pop removal.
pub struct BonusManager {
    pool: [BonusSlot; MAX_BONUSES],
    active_ids: [u16; MAX_BONUSES],
    active_count: i32,
    metrics: ViewportMetrics,
}

impl Default for BonusManager {
    fn default() -> Self {
        Self::new()
    }
}

impl BonusManager {
    pub fn new() -> Self {
        Self {
            pool: [BonusSlot::default(); MAX_BONUSES],
            active_ids: [0; MAX_BONUSES],
            active_count: 0,
            metrics: ViewportMetrics { win_w: 1280, win_h: 720, scale: 1.0 },
        }
    }

    pub fn set_metrics(&mut self, m: ViewportMetrics) {
        self.metrics = m;
    }

    pub fn add(&mut self, pos: Coord, bonus_type: BonusType) {
        if self.active_count >= MAX_BONUSES as i32 {
            return;
        }
        for idx in 0..MAX_BONUSES {
            if !self.pool[idx].active {
                let b = &mut self.pool[idx];
                b.tex = bonus_texture_id(bonus_type);
                b.pos = pos;
                b.speed = Coord::new(0, 1);
                b.bonus_type = bonus_type;
                b.active = true;
                self.active_ids[self.active_count as usize] = idx as u16;
                self.active_count += 1;
                return;
            }
        }
    }

    pub fn advance(&mut self) {
        let win_h = self.metrics.win_h;
        let mut i = 0i32;
        while i < self.active_count {
            let idx = self.active_ids[i as usize] as usize;
            if self.pool[idx].active {
                self.pool[idx].pos = self.pool[idx].pos.add(self.pool[idx].speed);
            }
            if self.pool[idx].out(win_h) {
                self.pool[idx].active = false;
                self.active_count -= 1;
                self.active_ids[i as usize] = self.active_ids[self.active_count as usize];
            } else {
                i += 1;
            }
        }
    }

    /// Twin of GetBonusCollision: first overlapping pod wins and is consumed.
    pub fn get_bonus_collision(&mut self, other: &GameObject) -> BonusType {
        if self.active_count <= 0 {
            return BonusType::None;
        }
        let mut i = 0i32;
        while i < self.active_count {
            let idx = self.active_ids[i as usize] as usize;
            if self.pool[idx].collide(other) {
                let res = self.pool[idx].bonus_type;
                self.pool[idx].active = false;
                self.active_count -= 1;
                self.active_ids[i as usize] = self.active_ids[self.active_count as usize];
                return res;
            }
            i += 1;
        }
        BonusType::None
    }

    pub fn nb(&self) -> i32 {
        self.active_count
    }

    pub fn get_active(&self, i: usize) -> &BonusSlot {
        &self.pool[self.active_ids[i] as usize]
    }
}
