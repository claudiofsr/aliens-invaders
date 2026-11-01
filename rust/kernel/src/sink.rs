//! Cosmetic-event boundary (port of src/simulation/presentation_sink.h).
//! The C++ shell forwards intents to Gfx/SoundManager; the golden dumper
//! records them as `<section>.sink` rows. The Rust twin collects the same
//! intents so the parity tests can replay the FULL observable behavior --
//! events are part of the contract, not decoration.

use crate::mathx::Coord;

#[derive(Clone, Debug, PartialEq)]
pub enum PresentationEvent {
    Text {
        x: i32,
        y: i32,
        r: u8,
        g: u8,
        b: u8,
        font_size: f32,
        ttl: i32,
        text: String,
    },
    Flash {
        r: u8,
        g: u8,
        b: u8,
        frames: i32,
    },
    Trauma(f32),
    Sfx {
        id: i32,
        pan: f32,
    },
}

/// Recording presentation sink: the headless twin of the game's Gfx/Sound
/// forwarding. `drain` mirrors RecordingSink::Drain (frame-stamped by the
/// caller, which owns the frame counter).
#[derive(Default)]
pub struct EventSink {
    pub events: Vec<PresentationEvent>,
}

impl EventSink {
    pub fn floating_text(
        &mut self,
        pos: Coord,
        text: &str,
        r: u8,
        g: u8,
        b: u8,
        font_size: f32,
        ttl_frames: i32,
    ) {
        self.events.push(PresentationEvent::Text {
            x: pos.x,
            y: pos.y,
            r,
            g,
            b,
            font_size,
            ttl: ttl_frames,
            text: text.to_string(),
        });
    }

    pub fn flash(&mut self, r: u8, g: u8, b: u8, frames: i32) {
        self.events.push(PresentationEvent::Flash { r, g, b, frames });
    }

    pub fn trauma(&mut self, amount: f32) {
        self.events.push(PresentationEvent::Trauma(amount));
    }

    pub fn sfx(&mut self, sound_id: i32, pan: f32) {
        self.events.push(PresentationEvent::Sfx { id: sound_id, pan });
    }

    pub fn drain(&mut self) -> Vec<PresentationEvent> {
        std::mem::take(&mut self.events)
    }
}
