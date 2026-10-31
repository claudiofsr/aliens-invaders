#ifndef GRAPHICS_GFX_SINK_H
#define GRAPHICS_GFX_SINK_H

#include "gfxinterface.h"
#include "presentation_sink.h"
#include "sdl_audio.h"
#include "texture_provider.h"

/// Production PresentationSink: forwards each simulation intent to the live Gfx
/// singleton and SoundManager -- exactly the pre-sink inline behavior.
class GfxPresentationSink final : public PresentationSink {
 public:
  explicit GfxPresentationSink(SoundManager* audio) noexcept : audio_(audio) {}

  void FloatingText(Coord pos, const char* text, std::uint8_t r, std::uint8_t g,
                    std::uint8_t b, float font_size, int ttl_frames) override {
    Gfx::Inst().AddFloatingText(pos, text, r, g, b, font_size, ttl_frames);
  }
  void Flash(std::uint8_t r, std::uint8_t g, std::uint8_t b, int frames) override {
    Gfx::Inst().TriggerFlash(r, g, b, frames);
  }
  void Trauma(float amount) override { Gfx::Inst().AddTrauma(amount); }
  void Sfx(int sound_id, float pan) override {
    if (audio_) {
      audio_->Play(static_cast<SoundManager::SoundEffect>(sound_id), pan);
    }
  }

 private:
  SoundManager* audio_;
};

/// Production TextureProvider: resolves ids through the live PixKeeper.
class PixKeeperTextureProvider final : public TextureProvider {
 public:
  [[nodiscard]] const Pix* Get(TextureId id) const override {
    return PixKeeper::Instance().Get(id);
  }
};

#endif  // GRAPHICS_GFX_SINK_H
