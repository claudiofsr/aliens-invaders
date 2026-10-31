#include "audio_export.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <string>

#include "constants.h"
#include "sdl_audio.h"   // SoftLimit21: shared with the live mixer (SSOT)

#ifdef _WIN32
#define AI_POPEN _popen
#define AI_PCLOSE _pclose
#else
#define AI_POPEN popen
#define AI_PCLOSE pclose
#endif

namespace fs = std::filesystem;

namespace AudioExport {
namespace {

// Playback-chain gains, computed exactly like SoundManager::Play() for a fanfare voice (volume 1.0,
// pan 0): left/right from cos/sin of pi/4 and the literal sub gain the mixer uses.
float ChainGainLeft() noexcept {
  const float angle = (0.0f + 1.0f) * (std::numbers::pi_v<float> * 0.25f);
  return 1.0f * std::cos(angle);
}
float ChainGainRight() noexcept {
  const float angle = (0.0f + 1.0f) * (std::numbers::pi_v<float> * 0.25f);
  return 1.0f * std::sin(angle);
}
constexpr float kChainSubGain = 1.0f * 0.7071f;

std::string LowerExt(const std::string& path) {
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

std::string ShellQuote(const std::string& s) {
  std::string out = "'";
  for (const char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

bool HaveCommand(const std::string& cmd) {
  FILE* p = AI_POPEN(("command -v " + ShellQuote(cmd) + " 2>/dev/null").c_str(), "r");
  if (p == nullptr) return false;
  char buf[512];
  const size_t n = std::fread(buf, 1, sizeof(buf) - 1, p);
  buf[n] = '\0';
  const int rc = AI_PCLOSE(p);
  return rc == 0 && n > 0;
}

struct Candidate {
  std::string tool;
  std::string cmd;
};

}  // namespace

std::vector<int16_t> RenderPlaybackPcm(const StageFanfare::Asset& asset) {
  const size_t frames = asset.subwoofer.size();
  std::vector<int16_t> pcm(frames * 2, 0);
  if (frames == 0 || asset.satellite.size() < frames * 2) return pcm;
  const float gl = ChainGainLeft();
  const float gr = ChainGainRight();
  for (size_t i = 0; i < frames; ++i) {
    const float sub = asset.subwoofer[i];
    const float l = asset.satellite[2 * i] * gl + sub * kChainSubGain;
    const float r = asset.satellite[2 * i + 1] * gr + sub * kChainSubGain;
    const float sl = SoftLimit21(l);
    const float sr = SoftLimit21(r);
    pcm[2 * i] = static_cast<int16_t>(std::clamp(sl * 32767.0f, -32767.0f, 32767.0f));
    pcm[2 * i + 1] = static_cast<int16_t>(std::clamp(sr * 32767.0f, -32767.0f, 32767.0f));
  }
  return pcm;
}

bool WriteWav(const std::string& path, const std::vector<int16_t>& pcm, int sample_rate) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
  const uint32_t riff_size = data_bytes + 36u;
  const uint32_t srate = static_cast<uint32_t>(sample_rate);
  const uint32_t byte_rate = srate * 4u;
  const uint32_t fmt_size = 16u;
  const uint16_t audio_fmt = 1u, channels = 2u, block_align = 4u, bits = 16u;
  auto wr = [f](const void* p, size_t n) { std::fwrite(p, 1, n, f); };
  wr("RIFF", 4); wr(&riff_size, 4); wr("WAVE", 4);
  wr("fmt ", 4); wr(&fmt_size, 4); wr(&audio_fmt, 2); wr(&channels, 2);
  wr(&srate, 4); wr(&byte_rate, 4); wr(&block_align, 2); wr(&bits, 2);
  wr("data", 4); wr(&data_bytes, 4);
  if (!pcm.empty()) wr(pcm.data(), pcm.size() * sizeof(int16_t));
  std::fclose(f);
  return true;
}

bool EncodeExternal(const std::string& path, const std::vector<int16_t>& pcm,
                    int sample_rate, std::string& used) {
  const std::string ext = LowerExt(path);
  const std::string q = ShellQuote(path);
  const std::string sr = std::to_string(sample_rate);
  const std::string khz = std::to_string(static_cast<double>(sample_rate) / 1000.0);
  const std::string ff = "ffmpeg -hide_banner -loglevel error -y -f s16le -ar " + sr + " -ac 2 -i - ";

  std::vector<Candidate> cands;
  if (ext == ".flac") {
    cands.push_back({"flac", "flac -f -s --force-raw-format --endian=little --sign=signed "
                             "--channels=2 --bps=16 --sample-rate=" + sr + " - -o " + q});
    cands.push_back({"ffmpeg", ff + q});
  } else if (ext == ".mp3") {
    cands.push_back({"lame", "lame -r -s " + khz + " --bitwidth 16 --signed --little-endian "
                             "-V 2 - " + q});
    cands.push_back({"ffmpeg", ff + "-b:a 192k " + q});
  } else if (ext == ".ogg") {
    cands.push_back({"oggenc", "oggenc --raw --raw-rate=" + sr + " --raw-channels=2 "
                               "--raw-bits=16 --signed --little-endian -Q - -o " + q});
    cands.push_back({"ffmpeg", ff + "-c:a libvorbis -q:a 6 " + q});
  } else if (ext == ".opus") {
    cands.push_back({"ffmpeg", ff + "-c:a libopus -b:a 128k " + q});
    cands.push_back({"opusenc", "opusenc --raw --raw-rate " + sr + " --raw-chan 2 "
                                "--raw-bits 16 --raw-endianness 0 --bitrate 128 - " + q});
  } else if (ext == ".m4a" || ext == ".mp4" || ext == ".aac") {
    cands.push_back({"ffmpeg", ff + "-c:a aac -b:a 192k " + q});
  } else {
    return false;   // unknown extension: caller falls back to native WAV
  }

  for (const Candidate& c : cands) {
    if (!HaveCommand(c.tool)) continue;
    std::error_code ec;
    fs::remove(path, ec);
    FILE* p = AI_POPEN(c.cmd.c_str(), "w");
    if (p == nullptr) continue;
    const size_t bytes = pcm.size() * sizeof(int16_t);
    const size_t wrote = (bytes > 0) ? std::fwrite(pcm.data(), 1, bytes, p) : 0u;
    const int rc = AI_PCLOSE(p);
    if (rc == 0 && wrote == bytes && fs::exists(path, ec) && fs::file_size(path, ec) > 0u) {
      used = c.tool;
      return true;
    }
    fs::remove(path, ec);   // never leave a partial file behind
  }
  return false;
}

}  // namespace AudioExport
