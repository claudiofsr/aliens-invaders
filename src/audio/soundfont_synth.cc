#include "soundfont_synth.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <numbers>
#include <string>
#include <vector>

#include "constants.h"

// Vendored third-party single-header synth (MIT, tsf.h kept verbatim): warnings from its C code are
// not ours to fix. CMake relaxes -Wundef for this source (GCC ignores diagnostic pragmas for ...
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wcast-align"
#endif
#define TSF_IMPLEMENTATION
#include "tsf.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;

namespace {

tsf* g_bank = nullptr;
std::string g_path;
std::once_flag g_once;
// The tsf instance is not thread-safe: the game synthesis worker and the
// CLI jukebox can bake concurrently, so renders are serialized here.
std::mutex g_render_mutex;

bool g_preferred = true;    // SoundFont bank is the default engine (-render opts out)

constexpr float kSubLevel = 0.60f;   // LFE derivation level from the stereo mix

std::vector<fs::path> SearchDirs() {
  std::vector<fs::path> dirs;
  dirs.emplace_back("assets/soundfonts");   // repo/dev layout, relative to cwd
  if (const char* home = std::getenv("HOME")) {
    const fs::path h(home);
    dirs.push_back(h / ".local/share/aliens-invaders/soundfonts");
    dirs.push_back(h / ".local/share/soundfonts");
  }
  dirs.emplace_back("/usr/local/share/aliens-invaders/soundfonts");
  dirs.emplace_back("/usr/local/share/soundfonts");
  dirs.emplace_back("/usr/share/aliens-invaders/soundfonts");
  dirs.emplace_back("/usr/share/soundfonts");   // distro GM banks live here
#ifdef __APPLE__
  dirs.emplace_back("/Library/Audio/Sounds/Banks");
#elif defined(_WIN32)
  // Per-user Windows spots: the portable profile mirror and the game AppData.
  if (const char* profile = std::getenv("USERPROFILE")) {
    dirs.push_back(fs::path(profile) / ".local/share/aliens-invaders/soundfonts");
  }
  if (const char* appdata = std::getenv("APPDATA")) {
    dirs.push_back(fs::path(appdata) / "aliens-invaders/soundfonts");
  }
#endif
  return dirs;
}

// First *.sf2 of the first directory that holds one; ties break
// alphabetically so the choice is deterministic.
fs::path FindSoundFont() {
  if (const char* env = std::getenv("ALIENS_SOUNDFONT")) {
    std::error_code ec;
    if (*env != '\0' && fs::is_regular_file(env, ec)) return fs::path(env);
  }
  for (const fs::path& dir : SearchDirs()) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    fs::path best;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      std::error_code fec;
      if (!entry.is_regular_file(fec)) continue;
      const fs::path p = entry.path();
      if (p.extension() != ".sf2") continue;
      if (best.empty() || p < best) best = p;   // alphabetical: deterministic
    }
    if (!best.empty()) return best;
  }
  return {};
}

void InitBank() {
  const fs::path path = FindSoundFont();
  if (path.empty()) {
    // Actionable one-time notice (printed once from the baker thread): the procedural fallback is a
    // documented choice, not a silent loss. It names the recommended bank (FluidR3 GM, MIT), ...
    std::fprintf(stderr,
                 "[audio] no SoundFont bank (.sf2) found; procedural synth engaged.\n"
                 "[audio] Tip: install the MIT-licensed FluidR3 GM bank (~141 MB) for\n"
                 "[audio] CD-quality TinySoundFont audio -- real instrument samples, higher\n"
                 "[audio] fidelity than the procedural synth. Distro packages: soundfont-fluid\n"
                 "[audio] (Arch/Manjaro), fluid-soundfont-gm (Debian/Ubuntu/Fedora); or drop any\n"
                 "[audio] GM .sf2 into assets/soundfonts/, or set ALIENS_SOUNDFONT=/path/bank.sf2\n");
    return;
  }
  g_bank = tsf_load_filename(path.string().c_str());
  if (g_bank == nullptr) {
    std::fprintf(stderr, "[audio] cannot load %s; procedural synth engaged\n",
                 path.string().c_str());
    return;
  }
  g_path = path.string();
  std::fprintf(stderr, "[audio] soundfont bank: %s\n", g_path.c_str());
}

// tsf_reset drops channel state, so channels are (re)armed on every render.
void ConfigureChannels(tsf* f, int sample_rate) {
  tsf_set_output(f, TSF_STEREO_INTERLEAVED, sample_rate, 0.0f);
  for (int v = 0; v < SoundFontSynth::kEnsembleVoices; ++v) {
    tsf_channel_set_bank_preset(f, v, 0, SoundFontSynth::kGmPrograms[v]);
    tsf_channel_set_pan(f, v, SoundFontSynth::kStagePan[v]);
  }
}

struct NoteEvent {
  size_t pos;
  uint8_t voice;
  uint8_t midi;
  float vel;
  bool on;
};

size_t SamplePos(float sec, int sample_rate, size_t total) {
  const double p = static_cast<double>(sec) * static_cast<double>(sample_rate);
  if (!(p > 0.0)) return 0;
  if (p >= static_cast<double>(total)) return total;
  return static_cast<size_t>(p);
}

}  // namespace

namespace SoundFontSynth {

bool EnsureReady() {
  std::call_once(g_once, InitBank);
  return g_bank != nullptr;
}

StageFanfare::Asset Render(int stage, int sample_rate, float start_sec, float end_sec) {
  std::lock_guard<std::mutex> render_lock(g_render_mutex);
  StageFanfare::Asset asset;
  if (g_bank == nullptr || sample_rate <= 0) return asset;

  const float full = StageFanfare::PieceDurationSeconds(stage);
  const float win_start = std::clamp(start_sec, 0.0f, full);
  const float win_end = std::clamp(end_sec, win_start, full);
  const float window_len = win_end - win_start;
  const size_t total = (window_len > 0.0f)
      ? static_cast<size_t>(static_cast<double>(sample_rate) * static_cast<double>(window_len))
      : 0u;
  asset.satellite.assign(total * 2, 0.0f);
  asset.subwoofer.assign(total, 0.0f);
  if (total == 0) return asset;

  tsf* f = g_bank;
  tsf_reset(f);
  // Hard flush: tsf_reset only schedules zero-release tails, so leftover voices would bleed into
  // the first samples of this render and make bakes history-dependent. Clearing the pool makes ...
  for (int v = 0; v < f->voiceNum; ++v) f->voices[v].playingPreset = -1;
  ConfigureChannels(f, sample_rate);

  // The written score becomes note-on/note-off events on the sample grid.
  const std::vector<StageFanfare::ScoreNote> score = StageFanfare::BuildScore(stage);
  std::vector<NoteEvent> events;
  events.reserve(score.size() * 2);
  for (const auto& n : score) {
    if (n.start >= win_end) continue;              // starts after the window
    if (n.start + n.dur <= win_start) continue;    // ends before the window
    const size_t on = SamplePos(n.start - win_start, sample_rate, total);
    if (on >= total) continue;
    size_t off = SamplePos(n.start + n.dur - win_start, sample_rate, total);
    if (off <= on) off = std::min(total, on + 1);  // at least one frame
    events.push_back({on, n.voice, static_cast<uint8_t>(n.midi), n.vel, true});
    events.push_back({off, n.voice, static_cast<uint8_t>(n.midi), n.vel, false});
  }
  std::stable_sort(events.begin(), events.end(), [](const NoteEvent& a, const NoteEvent& b) {
    if (a.pos != b.pos) return a.pos < b.pos;
    return !a.on && b.on;                          // offs before ons, same sample
  });

  // Offline event-driven render, straight into the interleaved stereo bus.
  size_t cur = 0;
  for (const NoteEvent& ev : events) {
    if (ev.pos > cur) {
      tsf_render_float(f, asset.satellite.data() + cur * 2,
                       static_cast<int>(ev.pos - cur), 0);
      cur = ev.pos;
    }
    if (ev.on) tsf_channel_note_on(f, ev.voice, ev.midi, ev.vel);
    else tsf_channel_note_off(f, ev.voice, ev.midi);
  }
  if (cur < total) {
    tsf_render_float(f, asset.satellite.data() + cur * 2,
                     static_cast<int>(total - cur), 0);
  }

  // LFE: derive the sub bus from the stereo mix (two cascaded 85 Hz poles).
  {
    constexpr float kPiLocal = std::numbers::pi_v<float>;
    const float a = 1.0f - std::exp(-2.0f * kPiLocal * 85.0f / static_cast<float>(sample_rate));
    float s1 = 0.0f;
    float s2 = 0.0f;
    for (size_t i = 0; i < total; ++i) {
      const float mono = (asset.satellite[2 * i] + asset.satellite[2 * i + 1]) * 0.5f;
      s1 += a * (mono - s1);
      s2 += a * (s1 - s2);
      asset.subwoofer[i] = s2 * kSubLevel;
    }
  }

  // Shared hall image, ear guard, fades and normalization: identical contract to the procedural
  // renderer, so both backends drop into the same 2.1 mixer at the same perceived loudness and ...
  StageFanfare::ApplyHallReverb(asset, sample_rate);
  StageFanfare::ApplyEarGuardShelf(asset, sample_rate);
  StageFanfare::FinalizeWindow(asset, sample_rate);
  return asset;
}

StageFanfare::Asset RenderAuto(int stage, int sample_rate, float start_sec, float end_sec) {
  // SoundFont bank by default; it still needs a discovered .sf2 -- the
  // procedural fallback (-render, or no bank) keeps zero-asset builds working.
  return (g_preferred && EnsureReady())
             ? Render(stage, sample_rate, start_sec, end_sec)
             : StageFanfare::Generate(stage, sample_rate, start_sec, end_sec);
}

void SetPreferred(bool preferred) noexcept { g_preferred = preferred; }

bool Preferred() noexcept { return g_preferred; }

}  // namespace SoundFontSynth
