#ifndef AUDIO_EXPORT_H
#define AUDIO_EXPORT_H

#include <cstdint>
#include <string>
#include <vector>

#include "stage_fanfare.h"

// Pure (SDL-free) export helpers shared by the CLI jukebox. The PCM path reproduces the live mixer
// chain for one centered fanfare voice (Play(): volume 1.0, pan 0), so an export is ...
namespace AudioExport {

/// Interleaved int16 stereo PCM through the playback chain: the exact
/// gains, SoftLimit21 and int16 clamp of AudioStreamCallback.
[[nodiscard]] std::vector<int16_t> RenderPlaybackPcm(const StageFanfare::Asset& asset);

/// Writes a 16-bit stereo RIFF WAV (little-endian hosts). True on success.
bool WriteWav(const std::string& path, const std::vector<int16_t>& pcm, int sample_rate);

/// Streams raw s16le stereo PCM through an external encoder chosen by the file extension
/// (flac/mp3/ogg/opus/m4a/mp4/aac): dedicated CLI tools first, ffmpeg fallback. On success sets ...
bool EncodeExternal(const std::string& path, const std::vector<int16_t>& pcm,
                    int sample_rate, std::string& used);

}  // namespace AudioExport

#endif  // AUDIO_EXPORT_H
