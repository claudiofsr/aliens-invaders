#ifndef SIMULATION_TEXTURE_PROVIDER_H
#define SIMULATION_TEXTURE_PROVIDER_H

#include "embedded_assets.h"  // TextureId vocabulary

class Pix;

/// Texture-resolution boundary of the pure simulation (Slice 3): gameplay asks
/// for pixmaps by id; the shell decides where pixels come from.
class TextureProvider {
 public:
  virtual ~TextureProvider() = default;
  [[nodiscard]] virtual const Pix* Get(TextureId id) const = 0;
};

#endif  // SIMULATION_TEXTURE_PROVIDER_H
