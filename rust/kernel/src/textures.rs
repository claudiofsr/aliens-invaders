//! Headless texture vocabulary: the `TextureId` enum of
//! src/platform/embedded_assets.h plus the canonical dimension table the
//! golden dumper records as `ent.pix` rows. The simulation only ever reads
//! Width()/Height() of a pixmap, so this table is the entire pixel-side
//! contract of the entity layer -- asserted row by row against the corpus.

/// Discriminants match the C++ `enum class TextureId : uint8_t` order.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
#[repr(u8)]
pub enum TextureId {
    None = 0,
    Player = 1,
    PlayerAlt = 2,
    Bullet = 3,
    Bomb = 4,
    BonusFire = 5,
    BonusShield = 6,
    BonusSpeed = 7,
    BonusMulti = 8,
    BonusNuke = 9,
    Alien1 = 10,
    Alien2 = 11,
    Alien3 = 12,
    Alien4 = 13,
    Alien5 = 14,
    Alien6 = 15,
    Alien7 = 16,
    Alien8 = 17,
    Alien9 = 18,
    Alien10 = 19,
    Alien11 = 20,
    Alien12 = 21,
    Alien13 = 22,
    Alien14 = 23,
    Alien15 = 24,
    Count = 25,
}

/// Base (scale-1) pixmap dimensions per id -- the `ent.pix` corpus rows.
pub const HEADLESS_DIMS: [(i32, i32); TextureId::Count as usize] = [
    (0, 0),     // None
    (48, 32),   // Player
    (48, 32),   // PlayerAlt
    (8, 16),    // Bullet
    (12, 20),   // Bomb
    (28, 28),   // BonusFire
    (28, 28),   // BonusShield
    (28, 28),   // BonusSpeed
    (28, 28),   // BonusMulti
    (28, 28),   // BonusNuke
    (36, 30),   // Alien1
    (36, 30),   // Alien2
    (36, 30),   // Alien3
    (40, 34),   // Alien4
    (36, 30),   // Alien5
    (36, 30),   // Alien6
    (36, 30),   // Alien7
    (36, 30),   // Alien8
    (36, 30),   // Alien9
    (36, 30),   // Alien10
    (36, 30),   // Alien11
    (36, 30),   // Alien12
    (44, 36),   // Alien13
    (40, 34),   // Alien14
    (44, 36),   // Alien15
];

/// Headless stand-in for `TextureProvider::Get`: the simulation reads only
/// the dimensions, so the id -> (w, h) lookup fully replaces the pixmap.
/// Corpus-parsing helper: maps the `static_cast<int>(TextureId)` values the
/// golden dumper prints back to the enum (rows ent.fleet.convoy / ent.pl.drop).
pub fn from_id(n: i32) -> TextureId {
    match n {
        0 => TextureId::None,
        1 => TextureId::Player,
        2 => TextureId::PlayerAlt,
        3 => TextureId::Bullet,
        4 => TextureId::Bomb,
        5 => TextureId::BonusFire,
        6 => TextureId::BonusShield,
        7 => TextureId::BonusSpeed,
        8 => TextureId::BonusMulti,
        9 => TextureId::BonusNuke,
        10 => TextureId::Alien1,
        11 => TextureId::Alien2,
        12 => TextureId::Alien3,
        13 => TextureId::Alien4,
        14 => TextureId::Alien5,
        15 => TextureId::Alien6,
        16 => TextureId::Alien7,
        17 => TextureId::Alien8,
        18 => TextureId::Alien9,
        19 => TextureId::Alien10,
        20 => TextureId::Alien11,
        21 => TextureId::Alien12,
        22 => TextureId::Alien13,
        23 => TextureId::Alien14,
        24 => TextureId::Alien15,
        _ => panic!("texture id out of range: {n}"),
    }
}

/// Twin of the C++ `static_cast<TextureId>(Alien1 + n)` wanderer sprite roll:
/// offset 0..14 maps to Alien1..Alien15 (kAlienTextureCount = 15).
pub fn alien_sprite(offset: i32) -> TextureId {
    match offset {
        0 => TextureId::Alien1,
        1 => TextureId::Alien2,
        2 => TextureId::Alien3,
        3 => TextureId::Alien4,
        4 => TextureId::Alien5,
        5 => TextureId::Alien6,
        6 => TextureId::Alien7,
        7 => TextureId::Alien8,
        8 => TextureId::Alien9,
        9 => TextureId::Alien10,
        10 => TextureId::Alien11,
        11 => TextureId::Alien12,
        12 => TextureId::Alien13,
        13 => TextureId::Alien14,
        _ => TextureId::Alien15,
    }
}

pub fn dims(id: TextureId) -> (i32, i32) {
    HEADLESS_DIMS[id as usize]
}

/// SoundManager::SoundEffect discriminants (src/audio/sdl_audio.h). The sink
/// rows carry these ids verbatim, so the vocabulary is part of the contract.
pub mod sfx {
    pub const PLAYER_FIRE: i32 = 0;
    pub const ALIEN_POP_LIGHT: i32 = 1;
    pub const ALIEN_POP_MEDIUM: i32 = 2;
    pub const ALIEN_POP_HEAVY: i32 = 3;
    pub const KAMIKAZE_ALERT: i32 = 4;
    pub const KAMIKAZE_EXPLODE: i32 = 5;
    pub const PLAYER_HIT: i32 = 6;
    pub const PLAYER_DESTRUCTION: i32 = 7;
    pub const POWERUP_FIRE: i32 = 8;
    pub const POWERUP_MULTI: i32 = 9;
    pub const POWERUP_SPEED: i32 = 10;
    pub const SHIELD_RESTORE: i32 = 11;
    pub const GAME_OVER: i32 = 12;
    pub const NUKE: i32 = 13;
}
