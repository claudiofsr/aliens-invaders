//! Port of the Player starship (src/entities/managers.{h,cc}): lateral
//! movement with float accumulation, the multi-shot fire cadence, the bomb
//! collision/shield economy and the supply-pod pickup effects.
//!
//! Scope notes (RUST.md slice 4b): the C++ twin holds manager pointers as
//! const members; the Rust twin takes them as parameters per call so the
//! harness can destructure its world without shared ownership. Draw/OnResize
//! are presentation-only and absent; hit_timer_ is render-side invulnerability
//! blink state with no simulation reader inside the corpus window -- kept as a
//! faithful field (the tick mutates it) but never gates anything, exactly
//! like the C++.

use crate::bonus::{BonusManager, BonusType};
use crate::bullets::{BulletsManager, SEEKER_ENGINE_COLOR};
use crate::explosions::ExplosionsManager;
use crate::fleet::Fleet;
use crate::game_object::GameObject;
use crate::mathx::{fast_round_f32, Coord};
use crate::score::Score;
use crate::sink::EventSink;
use crate::stage_catalog;
use crate::textures::{dims, TextureId};
use crate::trajectory::ViewportMetrics;

// GameRules::Player / GameRules::Visuals constants (golden-vector policed).
const PLAYER_BASE_BULLET_SPEED: f32 = -8.0;
/// GameRules::Player::kPlayerBottomMarginPx: the player baseline sits this
/// many pixels above the window bottom (`pos_y`). Public because scenario
/// harnesses place entities relative to the real baseline -- the spawn
/// placeholder (-50) is not it.
pub const PLAYER_BOTTOM_MARGIN_PX: i32 = 48;
const INITIAL_SHIELDS: i32 = 3;
const PLAYER_MAX_SHIELD: i32 = 8;
const PLAYER_SHIELD_GATE_THRESHOLD: i32 = 4;
const PLAYER_MAX_SPEED_LEVEL: i32 = 5;
const PLAYER_MAX_FIRE_LEVEL: i32 = 2;
const PLAYER_MAX_MULTI_SHOTS: i32 = 3;
const DAMAGE_IMMUNITY_FRAMES: i32 = 42;
const SPEED_BOOST_PERCENT: f32 = 0.30;
const FIRE_RATE_BOOST_PERCENT: f32 = 0.10;
const BASE_FIRE_INTERVAL_FRAMES: f32 = 40.0;
const FLOATING_TEXT_POWERUP_FONT_SIZE: f32 = 35.0;
const FLOATING_TEXT_SHIELD_FONT_SIZE: f32 = 36.0;
const FLOATING_TEXT_NUKE_BANNER_FONT_SIZE: f32 = 42.0;

pub struct Player {
    object: GameObject,
    shield: i32,
    speed_level: i32,
    fire_level: i32,
    fire_wait: i32,
    fire_interval: i32,
    multi_fire: i32,
    hit_timer: i32,
    fx: f32,
    prev_fx: f32,
    metrics: ViewportMetrics,
}

impl Player {
    /// Twin of the Player constructor: centered off-screen spawn (the first
    /// Move drops the ship onto its baseline row).
    pub fn new(metrics: ViewportMetrics) -> Self {
        let (w, h) = dims(TextureId::Player);
        let pos = Coord::new(metrics.win_w / 2, -50);
        Self {
            object: GameObject::new(pos, w, h),
            shield: INITIAL_SHIELDS,
            speed_level: 0,
            fire_level: 0,
            fire_wait: 0,
            fire_interval: BASE_FIRE_INTERVAL_FRAMES as i32,
            multi_fire: 1,
            hit_timer: 0,
            fx: pos.x as f32,
            prev_fx: pos.x as f32,
            metrics,
        }
    }

    pub fn set_metrics(&mut self, m: ViewportMetrics) {
        self.metrics = m;
    }

    /// Read-only subpixel carry for the ent.rz.player rows (Fatia 13).
    pub fn fx(&self) -> f32 {
        self.fx
    }

    /// Twin of Player::OnResize (managers.cc): only the x axis rescales (by
    /// float FastRound) and re-clamps against the NEW viewport -- the metrics
    /// snapshot is already distributed when the cascade runs. The y axis is
    /// RE-BASELINED through PosY() instead of scaled: the bottom-margin anchor
    /// is resolution-relative, so the player always lands on the fresh floor
    /// row (the ent.rz.player rows pin y != old_y * ry). fx_/prev_fx_ collapse
    /// onto the integer position, restarting the subpixel carry clean.
    pub fn on_resize(&mut self, rx: f32, _ry: f32) {
        let mut p = self.object.pos;
        p.x = fast_round_f32(p.x as f32 * rx);
        let max_right = self.metrics.win_w - self.width() / 2 - 1;
        if p.x > max_right {
            p.x = max_right;
        }
        if p.x < self.width() / 2 {
            p.x = self.width() / 2;
        }
        p.y = self.pos_y();
        self.object.pos = p;
        self.fx = p.x as f32;
        self.prev_fx = self.fx;
    }

    pub fn position(&self) -> Coord {
        self.object.pos
    }

    pub fn width(&self) -> i32 {
        self.object.width()
    }

    pub fn height(&self) -> i32 {
        self.object.height()
    }

    pub fn game_object(&self) -> &GameObject {
        &self.object
    }

    pub fn shield(&self) -> i32 {
        self.shield
    }

    pub fn fire_level(&self) -> i32 {
        self.fire_level
    }

    pub fn multi_fire(&self) -> i32 {
        self.multi_fire
    }

    pub fn speed_level(&self) -> i32 {
        self.speed_level
    }

    pub fn is_fire_maxed(&self) -> bool {
        self.fire_level >= PLAYER_MAX_FIRE_LEVEL
    }

    pub fn is_multi_maxed(&self) -> bool {
        self.multi_fire >= PLAYER_MAX_MULTI_SHOTS
    }

    pub fn is_speed_maxed(&self) -> bool {
        self.speed_level >= PLAYER_MAX_SPEED_LEVEL
    }

    pub fn is_shield_gated(&self) -> bool {
        self.shield > PLAYER_SHIELD_GATE_THRESHOLD
    }

    pub fn is_shield_maxed(&self) -> bool {
        self.shield >= PLAYER_MAX_SHIELD
    }

    /// PosY twin: fixed baseline row pinned to the bottom margin.
    fn pos_y(&self) -> i32 {
        self.metrics.win_h - PLAYER_BOTTOM_MARGIN_PX - self.height() / 2
    }

    /// Twin of Player::Move: float accumulation with a double clamp (float
    /// hull bounds first, then the integer pixel bounds).
    pub fn move_player(&mut self, direction: i32) {
        if self.fire_wait > 0 {
            self.fire_wait -= 1;
        }
        if self.hit_timer > 0 {
            self.hit_timer -= 1;
        }

        let s = self.metrics.scale;
        let current_speed = stage_catalog::player_compute_speed(self.speed_level);

        self.prev_fx = self.fx;
        self.fx += direction as f32 * (current_speed * s);
        let half_w = self.width() as f32 * 0.5;
        let max_r = self.metrics.win_w as f32 - half_w - 1.0;
        self.fx = self.fx.clamp(half_w, max_r);

        let mut pos = self.object.pos;
        pos.x = fast_round_f32(self.fx);
        if pos.x < self.width() / 2 {
            pos.x = self.width() / 2;
        } else {
            let max_right = self.metrics.win_w - self.width() / 2 - 1;
            if pos.x > max_right {
                pos.x = max_right;
            }
        }
        pos.y = self.pos_y();
        self.object.pos = pos;
    }

    /// Twin of Player::Fire: cadence gate, pan-derived shot sfx and the
    /// odd/even symmetric multi-shot spread.
    pub fn fire(&mut self, bullets: &mut BulletsManager, sink: &mut EventSink) {
        if self.fire_wait > 0 {
            return;
        }
        self.fire_wait = self.fire_interval;

        let pan = (self.position().x as f32 - self.metrics.win_w as f32 * 0.5)
            / (self.metrics.win_w as f32 * 0.5);
        sink.sfx(0, pan * 0.65);

        let s = self.metrics.scale;
        let b_speed_y = (-4).min(fast_round_f32(PLAYER_BASE_BULLET_SPEED * s));

        for i in 0..self.multi_fire {
            let mut speed = Coord::new(
                fast_round_f32((i - self.multi_fire / 2) as f32 * s),
                b_speed_y,
            );
            if self.multi_fire % 2 == 0 && i >= self.multi_fire / 2 {
                speed.x += 1;
            }
            let mut fire_pos = self.position();
            fire_pos.y -= self.height() / 2;
            bullets.add(
                TextureId::Bullet,
                fire_pos,
                speed,
                false,
                false,
                0,
                0.0,
                false,
                SEEKER_ENGINE_COLOR,
            );
        }
    }

    /// Twin of Player::DoBombsCollisions: up to shield+1 simultaneous hits,
    /// immunity window and one debris burst per absorbed bomb.
    pub fn do_bombs_collisions(
        &mut self,
        bombs: &mut BulletsManager,
        explosions: &mut ExplosionsManager,
        sink: &mut EventSink,
    ) {
        let hits = bombs.do_collisions(&self.object, self.shield + 1);
        if hits > 0 {
            self.shield -= hits;
            self.hit_timer = DAMAGE_IMMUNITY_FRAMES;
            sink.flash(255, 40, 40, 8);
            sink.trauma(0.50);
            sink.sfx(6, 0.0);
            for _ in 0..hits {
                explosions.add(self.position(), Coord::new(0, 0), 255, 80, 80, 45);
            }
        }
    }

    /// Twin of Player::DoBonusCollisions: drains every overlapping pod this
    /// frame. The sfx precedes the upgrade, and each banner reflects the
    /// POST-increment maxed state -- both orderings are part of the corpus.
    pub fn do_bonus_collisions(
        &mut self,
        bonus: &mut BonusManager,
        explosions: &mut ExplosionsManager,
        sink: &mut EventSink,
        mut fleet: Option<&mut Fleet>,
        mut score: Option<&mut Score>,
    ) {
        loop {
            let res = bonus.get_bonus_collision(&self.object);
            if res == BonusType::None {
                break;
            }
            match res {
                BonusType::ExtraSpeed => {
                    sink.sfx(10, 0.0);
                    self.extra_speed();
                    let spd_step_pct = fast_round_f32(SPEED_BOOST_PERCENT * 100.0);
                    let spd_max_pct = 100 + spd_step_pct * PLAYER_MAX_SPEED_LEVEL;
                    let buf = if self.is_speed_maxed() {
                        format!("SPEED +{spd_step_pct}% [{spd_max_pct}% MAX]!")
                    } else {
                        format!("SPEED +{spd_step_pct}%!")
                    };
                    sink.floating_text(
                        self.position(),
                        &buf,
                        100,
                        220,
                        255,
                        FLOATING_TEXT_POWERUP_FONT_SIZE,
                        120,
                    );
                }
                BonusType::ExtraFire => {
                    sink.sfx(8, 0.0);
                    self.extra_fire();
                    let fr_step_pct = fast_round_f32(FIRE_RATE_BOOST_PERCENT * 100.0);
                    let fr_max_pct = 100 + fr_step_pct * PLAYER_MAX_FIRE_LEVEL;
                    let buf = if self.is_fire_maxed() {
                        format!("FIRE RATE +{fr_step_pct}% [{fr_max_pct}% MAX]!")
                    } else {
                        format!("FIRE RATE +{fr_step_pct}%!")
                    };
                    sink.floating_text(
                        self.position(),
                        &buf,
                        255,
                        120,
                        120,
                        FLOATING_TEXT_POWERUP_FONT_SIZE,
                        120,
                    );
                }
                BonusType::ExtraMulti => {
                    sink.sfx(9, 0.0);
                    self.extra_multi();
                    let buf = if self.is_multi_maxed() {
                        format!(
                            "{PLAYER_MAX_MULTI_SHOTS}-WAY MULTI [MAX {PLAYER_MAX_MULTI_SHOTS}]!"
                        )
                    } else {
                        "MULTI CANNON +1!".to_string()
                    };
                    sink.floating_text(
                        self.position(),
                        &buf,
                        180,
                        120,
                        255,
                        FLOATING_TEXT_POWERUP_FONT_SIZE,
                        120,
                    );
                }
                BonusType::ExtraShield => {
                    sink.sfx(11, 0.0);
                    self.extra_shield();
                    let buf = if self.is_shield_maxed() {
                        format!("SHIELD PROTECTION [MAX {PLAYER_MAX_SHIELD}]!")
                    } else {
                        "+1 SHIELD PROTECTION!".to_string()
                    };
                    sink.floating_text(
                        self.position(),
                        &buf,
                        50,
                        255,
                        130,
                        FLOATING_TEXT_SHIELD_FONT_SIZE,
                        120,
                    );
                }
                BonusType::ExtraNuke => {
                    sink.sfx(13, 0.0);
                    sink.flash(255, 240, 180, 16);
                    sink.trauma(0.95);
                    sink.floating_text(
                        self.position(),
                        "TACTICAL NUKE!",
                        255,
                        220,
                        40,
                        FLOATING_TEXT_NUKE_BANNER_FONT_SIZE,
                        120,
                    );
                    if let Some(f) = fleet.as_deref_mut() {
                        f.nuke_all(explosions, sink, score.as_deref_mut());
                    }
                }
                BonusType::None => unreachable!(),
            }
        }
    }

    pub fn extra_fire(&mut self) {
        if self.fire_level < PLAYER_MAX_FIRE_LEVEL {
            self.fire_level += 1;
            self.fire_interval = stage_catalog::player_compute_fire_interval(self.fire_level);
        }
    }

    pub fn extra_multi(&mut self) {
        if self.multi_fire < PLAYER_MAX_MULTI_SHOTS {
            self.multi_fire += 1;
        }
    }

    pub fn extra_shield(&mut self) {
        if self.shield < PLAYER_MAX_SHIELD {
            self.shield += 1;
        }
    }

    pub fn extra_speed(&mut self) {
        if self.speed_level < PLAYER_MAX_SPEED_LEVEL {
            self.speed_level += 1;
        }
    }
}
