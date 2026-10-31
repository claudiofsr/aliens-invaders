#include "cli_soundtrack.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "audio_export.h"
#include "cli_flags.h"
#include "constants.h"
#include "random_stream.h"
#include "sdl_audio.h"
#include "sdl_renderer.h"
#include "soundfont_synth.h"
#include "stage_fanfare.h"
#include "track_selection.h"
#include "version.h"

namespace CliSoundtrack {

namespace {

std::atomic<bool> g_interrupted{false};

void SignalHandler(int) {
  g_interrupted.store(true, std::memory_order_relaxed);
}

/// Prints the command-line manual. Documentation policy (why it looks like this): - Each option
/// exposes exactly two spellings -- the long name and, when one exists, its short letter (e.g. ...
void PrintHelp() {
  std::cout << "\033[1m==============================================================================\033[0m\n"
            << " \033[1;36mALIENS INVADERS — COMMAND LINE REFERENCE\033[0m\n"
            << "\033[1m==============================================================================\033[0m\n"
            << "  \033[1mUsage:\033[0m\n"
            << "    aliens-invaders                       Launch game (Default: Vulkan with OpenGL fallback)\n"
            << "    aliens-invaders -driver <name>        Launch game with the specified graphics driver\n"
            << "    aliens-invaders -play <a> <b>         Play soundtrack movements <a> through <b> in terminal\n"
            << "    aliens-invaders -play 6,1,15,2,8      Play exactly these tracks, in this order\n"
            << "    aliens-invaders -play 1 -s 6 -e 30    Play track 1 from second 6 to second 30\n"
            << "    aliens-invaders -play 1 15 -shuffle   Play tracks 1 to 15 in random order\n\n"
            << "  \033[1mOptions:\033[0m\n"
            << "    -driver, -d <name>          Launch with a specific graphics driver (e.g. -driver opengl)\n"
            << "    -backends, -b               List all available 2D graphics rendering backends (SDL3 drivers)\n"
            << "    -list, -l                   List all 15 classical soundtrack movements\n"
            << "    -play, -p <tracks>          Play track N, range 1 15, or comma list 6,1,15,2,8 (any\n"
            << "                                order, repeats ok); each track COMPLETE by default\n"
            << "    -shuffle                    Jukebox: play the -play selection in random order\n"
            << "                                (long form only: the short -s belongs to -start)\n"
            << "    -start, -s <sec>            Jukebox: open the window at <sec> seconds (default 0)\n"
            << "    -end, -e <sec>              Jukebox: close the window at <sec> seconds\n"
            << "                                (default: the track's full length; ends clamp to it)\n"
            << "    -output, -o <file>          Write the window to a file instead of playing it:\n"
            << "                                .wav native (zero deps); .flac/.mp3/.ogg/.opus/.m4a\n"
            << "                                piped through ffmpeg/flac/lame/oggenc/opusenc when\n"
            << "                                installed (graceful .wav fallback otherwise)\n"
            << "    -render, -r                 Render through the procedural synth instead of\n"
            << "                                the default TinySoundFont bank (.sf2) (game and jukebox)\n"
            << "    -log                        Arm the frame-hitch flight recorder (env: ALIENS_HITCH_LOG=1)\n"
            << "    -version, -v                Print the version banner and exit\n"
            << "    -help, -h                   Display this command-line manual\n\n"
            << "  \033[1mNote:\033[0m\n"
            << "    Flags are dash-agnostic: one or more leading dashes are equivalent,\n"
            << "    so -version, --version and ---version all select the same option.\n"
            << "    The stage fanfare heard in game plays each movement's climatic\n"
            << "    six-second preset (peak_start_seconds); the jukebox windows above\n"
            << "    never touch it.\n"
            << "\033[1m==============================================================================\033[0m\n";
}

/// Prints the version banner. The version literal has a single source of truth (version.h): the
/// build systems inject VERSION_STRING, so the banner always reports the exact release the ...
void PrintVersion() {
  std::cout << "\033[1m==============================================================================\033[0m\n"
            << " \033[1;36mALIENS INVADERS — VERSION\033[0m\n"
            << "\033[1m==============================================================================\033[0m\n"
            << "  \033[1mVersion:\033[0m    " << VERSION_STRING << "\n"
            << "  \033[1mEngine:\033[0m     Modern C++20 • SDL3 (Vulkan default with OpenGL fallback)\n"
            << "  \033[1mLicense:\033[0m    MIT — Copyright (c) 2026 Claudio Fernandes de Souza Rodrigues\n"
            << "  \033[1mRepository:\033[0m https://github.com/claudiofsr/aliens-invaders\n"
            << "  \033[1mHint:\033[0m       Run 'aliens-invaders -help' for the full command-line manual.\n"
            << "\033[1m==============================================================================\033[0m\n";
}

void PrintDrivers() {
  const bool video_was_init = (SDL_WasInit(SDL_INIT_VIDEO) != 0);
  if (!video_was_init) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
      std::cerr << "[ERROR] Failed to query video subsystem: " << SDL_GetError() << "\n";
      return;
    }
  }

  const int count = SDL_GetNumRenderDrivers();
  std::cout << "\033[1m==============================================================================\033[0m\n"
            << " \033[1;36mALIENS INVADERS — AVAILABLE 2D GRAPHICS RENDERING BACKENDS (SDL3)\033[0m\n"
            << "\033[1m==============================================================================\033[0m\n"
            << "  Found " << count << " driver(s) compiled into SDL3 for this display:\n\n"
            << "  \033[1mIndex   Driver Name     Architecture / Description\033[0m\n"
            << "  ----------------------------------------------------------------------------\n";

  for (int i = 0; i < count; ++i) {
    const char* driver = SDL_GetRenderDriver(i);
    if (!driver) continue;

    const std::string_view name(driver);
    std::string desc = "Hardware accelerated rendering";
    if (name == "vulkan") {
      desc = "Vulkan 1.3+ low-overhead explicit GPU pipeline (Default / Recommended)";
    } else if (name == "opengl") {
      desc = "Desktop OpenGL 4.6 (Hardware accelerated)";
    } else if (name == "opengles2" || name == "opengles") {
      desc = "OpenGL ES 2.0 / 3.0 via EGL (Mobile / Embedded / Lightweight)";
    } else if (name == "gpu") {
      desc = "SDL3 GPU modern compute and rendering abstraction";
    } else if (name == "metal") {
      desc = "Apple Metal native graphics pipeline";
    } else if (name == "direct3d12") {
      desc = "Microsoft Direct3D 12 explicit GPU pipeline";
    } else if (name == "direct3d11" || name == "direct3d") {
      desc = "Microsoft Direct3D 11 hardware pipeline";
    } else if (name == "software") {
      desc = "CPU rasterization fallback (No GPU acceleration)";
    }

    std::cout << "    [" << (i + 1) << "]   \033[1;32m" << std::left << std::setw(15) << driver
              << "\033[0m " << desc << "\n";
  }

  std::cout << "  ----------------------------------------------------------------------------\n"
            << "  \033[1mUsage:\033[0m aliens-invaders -driver <name>   (e.g. aliens-invaders -driver opengl)\n"
            << "         aliens-invaders -d vulkan\n"
            << "\033[1m==============================================================================\033[0m\n";

  if (!video_was_init) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
}

bool IsDriverAvailable(const std::string& driver_name, std::vector<std::string>& available_out) {
  const bool video_was_init = (SDL_WasInit(SDL_INIT_VIDEO) != 0);
  if (!video_was_init) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
      return false;
    }
  }

  const int count = SDL_GetNumRenderDrivers();
  available_out.clear();
  available_out.reserve(static_cast<size_t>(count));

  bool found = false;
  for (int i = 0; i < count; ++i) {
    const char* d = SDL_GetRenderDriver(i);
    if (!d) continue;
    available_out.emplace_back(d);
    if (driver_name == d) {
      found = true;
    }
  }

  if (!video_was_init) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }

  return found;
}

void PrintPlaylist() {
  std::cout << "\033[1m===================================================================================================\033[0m\n"
            << " \033[1;36mALIENS INVADERS — SYMPHONIC SOUNDTRACK (10-INSTRUMENT CLASSICAL ENSEMBLE ANTHOLOGY)\033[0m\n"
            << "\033[1m===================================================================================================\033[0m\n"
            << "  CD Quality Stereo 2.1 | In-phase Sub-bass (< 85 Hz) | Zero Digital Clipping\n"
            << "---------------------------------------------------------------------------------------------------\n";

  for (int stage = 1; stage <= StageFanfare::kPieceCount; ++stage) {
    const auto& m = StageFanfare::GetMetadata(stage);
    std::cout << " \033[1;33m[" << std::setw(2) << stage << "]\033[0m "
              << "\033[1m" << m.composer << "\033[0m (" << m.nationality << ", " << m.life_dates << ")\n"
              << "      \033[1;32m" << m.title << " (" << m.opus_catalog << ")\033[0m — " << m.movement_phrase
              << " \033[0;90m[" << std::fixed << std::setprecision(1)
              << StageFanfare::PieceDurationSeconds(stage) << " s]\033[0m\n"
              << "      \033[0;90m“" << m.historical_curiosity << "”\033[0m\n\n";
  }

  std::cout << "---------------------------------------------------------------------------------------------------\n"
            << "  \033[1mFull lengths:\033[0m        -play <track> performs the COMPLETE movement by default\n"
            << "                       (0.0 s to the [s] value printed per track above);\n"
            << "                       -start/-end narrow the window to any slice.\n"
            << "  \033[1mTo listen from terminal:\033[0m aliens-invaders -play <track_number> (e.g. aliens-invaders -play 1 4)\n"
            << "  \033[1mTo choose a window:\033[0m      aliens-invaders -play 1 -s 6 -e 30\n"
            << "\033[1m===================================================================================================\033[0m\n";
}

bool PlayTrack(SoundManager& audio, int track, float window_start_sec, float window_end_sec) {
  if (track < 1 || track > StageFanfare::kPieceCount) return false;
  // Default window: the complete movement -- the -end sentinel resolves to
  // the track's own scored length, so banner, bake and progress agree.
  if (window_end_sec < 0.0f) window_end_sec = StageFanfare::PieceDurationSeconds(track);
  if (window_start_sec >= window_end_sec) {
    std::cerr << "[ERROR] -start " << std::fixed << std::setprecision(1)
              << window_start_sec << " s lies beyond track " << track
              << " (" << window_end_sec << " s).\n";
    return false;
  }
  const auto& m = StageFanfare::GetMetadata(track);

  std::cout << "\n\033[1m------------------------------------------------------------------------------\033[0m\n"
            << " \033[1;36mNOW PLAYING: Track [" << track << "/" << StageFanfare::kPieceCount << "]\033[0m\n"
            << "   • \033[1mComposer:\033[0m   " << m.composer << " (" << m.nationality << ", " << m.life_dates << ")\n"
            << "   • \033[1mWork:\033[0m       " << m.title << " (" << m.opus_catalog << ")\n"
            << "   • \033[1mMovement:\033[0m   " << m.movement_phrase << "\n"
            << "   • \033[1mWindow:\033[0m     " << std::fixed << std::setprecision(1) << window_start_sec
            << " s to " << window_end_sec << " s of "
            << StageFanfare::PieceDurationSeconds(track) << " s\n"
            << "   • \033[1mNote:\033[0m       \033[0;90m“" << m.historical_curiosity << "”\033[0m\n"
            << "\033[1m------------------------------------------------------------------------------\033[0m\n";

  // Headless jukebox: no frame budget, so a blocking bake is correct.
  // In the graphical game this call is forbidden (see PlayStageFanfare).
  audio.BakeFanfare(track, window_start_sec, window_end_sec);
  audio.Resume();
  const auto info = audio.PlayStageFanfare(track);
  const float duration = info.duration_seconds > 0.0f ? info.duration_seconds : 6.0f;

  const auto start_time = std::chrono::steady_clock::now();
  constexpr int kBarWidth = 36;

  while (!g_interrupted.load(std::memory_order_relaxed)) {
    const auto elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_time).count();
    if (elapsed >= duration) {
      std::cout << "\r  \033[1;32m[" << std::string(kBarWidth, '=') << "] 100.0% ("
                << std::fixed << std::setprecision(1) << duration << "s / " << duration << "s)\033[0m\n";
      break;
    }

    const float progress = std::clamp(elapsed / duration, 0.0f, 1.0f);
    const int filled = static_cast<int>(progress * static_cast<float>(kBarWidth));

    std::cout << "\r  [" << std::string(static_cast<size_t>(filled), '=');
    if (filled < kBarWidth) {
      std::cout << '>';
      std::cout << std::string(static_cast<size_t>(kBarWidth - filled - 1), ' ');
    }
    std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100.0f) << "% ("
              << elapsed << "s / " << duration << "s)  [Press Ctrl+C to stop]" << std::flush;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Allow hardware buffer to finish playing the smooth fade-out before clearing
  if (!g_interrupted.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  audio.Clear();
  audio.Pause();
  return !g_interrupted.load(std::memory_order_relaxed);
}

// Exports exactly what the playback chain would emit (voice gains, soft limiter, 16-bit): WAV
// natively, or raw PCM piped through an external encoder for compressed formats, with a ...
int RunExport(int track, float window_start_sec, float window_end_sec,
              const std::string& out_path) {
  if (track < 1 || track > StageFanfare::kPieceCount) return 1;
  // Default window: the complete movement (see PlayTrack's sentinel rule).
  if (window_end_sec < 0.0f) window_end_sec = StageFanfare::PieceDurationSeconds(track);
  const auto& m = StageFanfare::GetMetadata(track);
  const int sr = GameRules::Audio::kSampleRateHz;

  const bool use_bank = SoundFontSynth::Preferred() && SoundFontSynth::EnsureReady();
  std::cout << "\n[export] " << m.composer << " - " << m.title << "\n"
            << "[export] window " << std::fixed << std::setprecision(1)
            << window_start_sec << " s .. " << window_end_sec << " s | engine: "
            << (use_bank ? "SoundFont" : "procedural") << "\n";

  // Routing SSOT: RenderAuto already applies the preferred-engine choice (and its procedural
  // fallback), so the exporter never duplicates the decision here; use_bank above is the ...
  const StageFanfare::Asset asset =
      SoundFontSynth::RenderAuto(track, sr, window_start_sec, window_end_sec);
  const std::vector<int16_t> pcm = AudioExport::RenderPlaybackPcm(asset);
  if (pcm.empty()) {
    std::cerr << "[ERROR] empty render window; nothing to export.\n";
    return 1;
  }

  namespace fs = std::filesystem;
  std::string ext = fs::path(out_path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  auto report = [](const fs::path& p) {
    std::error_code ec;
    const double mb = static_cast<double>(fs::file_size(p, ec)) / (1024.0 * 1024.0);
    std::cout << "[OK] exported " << p << " (" << std::setprecision(2) << mb << " MB)\n";
  };

  if (ext.empty() || ext == ".wav") {
    const fs::path wav = ext.empty() ? fs::path(out_path + ".wav") : fs::path(out_path);
    if (!AudioExport::WriteWav(wav.string(), pcm, sr)) {
      std::cerr << "[ERROR] cannot write " << wav << "\n";
      return 1;
    }
    report(wav);
    return 0;
  }

  std::string used;
  if (AudioExport::EncodeExternal(out_path, pcm, sr, used)) {
    std::cout << "[export] encoder: " << used << "\n";
    report(fs::path(out_path));
    return 0;
  }

  const fs::path wav = fs::path(out_path).replace_extension(".wav");
  if (!AudioExport::WriteWav(wav.string(), pcm, sr)) {
    std::cerr << "[ERROR] cannot write " << wav << "\n";
    return 1;
  }
  std::cout << "[WARN] no working encoder for '" << ext
            << "' (install ffmpeg, flac, lame, oggenc or opusenc).\n";
  report(wav);
  return 0;
}

int RunJukebox(const std::vector<int>& tracks, float window_start_sec,
               float window_end_sec, bool shuffle) {
  g_interrupted.store(false, std::memory_order_relaxed);
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  if (!SDL_Init(SDL_INIT_AUDIO)) {
    std::cerr << "[ERROR] Failed to initialize SDL3 audio subsystem: " << SDL_GetError() << "\n";
    return 1;
  }

  {
    SoundManager audio;
    if (!audio.Init()) {
      std::cerr << "[ERROR] Failed to open audio device stream.\n";
      SDL_Quit();
      return 1;
    }

    // One-line selection summary for the banner: a single track, a
    // contiguous range, or an explicit list in the user's own order.
    std::string summary;
    if (tracks.size() == 1) {
      summary = "Track " + std::to_string(tracks.front());
    } else if (std::adjacent_find(tracks.begin(), tracks.end(),
                                  [](int a, int b) { return b != a + 1; }) ==
               tracks.end()) {
      summary = "Tracks " + std::to_string(tracks.front()) + " to " +
                std::to_string(tracks.back());
    } else {
      summary = "Tracks";
      for (const int t : tracks) summary += " " + std::to_string(t);
    }

    std::cout << "\033[1m==============================================================================\033[0m\n"
              << " \033[1;32mALIENS INVADERS — HEADLESS TERMINAL JUKEBOX (" << summary
              << " | Window " << std::fixed << std::setprecision(1) << window_start_sec << " s to ";
    if (window_end_sec < 0.0f) std::cout << "each track's end";
    else std::cout << window_end_sec << " s";
    std::cout << ")\033[0m\n"
              << "\033[1m==============================================================================\033[0m\n";

    // Playback order: the selection exactly as given; -shuffle draws a fresh permutation through
    // RandomStream (Fisher-Yates via std::shuffle), seeded from the wall clock so every run ...
    std::vector<int> order = tracks;
    if (shuffle && order.size() > 1) {
      const auto epoch_ns =
          std::chrono::system_clock::now().time_since_epoch().count();
      RandomStream rng(static_cast<std::uint32_t>(epoch_ns));
      std::shuffle(order.begin(), order.end(), rng);
      std::cout << "  \033[1mShuffle order:\033[0m ";
      for (size_t k = 0; k < order.size(); ++k) {
        if (k > 0) std::cout << ' ';
        std::cout << order[k];
      }
      std::cout << "\n";
    }

    for (size_t k = 0; k < order.size(); ++k) {
      if (g_interrupted.load(std::memory_order_relaxed)) break;
      const bool completed = PlayTrack(audio, order[k], window_start_sec, window_end_sec);
      if (!completed) break;

      if (k + 1 < order.size() && !g_interrupted.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
      }
    }

    audio.Quit();
  }

  SDL_Quit();

  if (g_interrupted.load(std::memory_order_relaxed)) {
    std::cout << "\n\033[1;33m[-] Playback interrupted by user.\033[0m\n";
  } else {
    std::cout << "\n\033[1;32m[OK] Playback finished successfully.\033[0m\n";
  }

  return 0;
}

}  // namespace

bool ProcessArgs(int argc, char* argv[], int& exit_code) {
  if (argc < 2) return false;

  // Jukebox window: -start/-s <sec> and -end/-e <sec> choose the slice of the track that plays; the
  // renderers clamp it to the track's own scored length. A pre-pass reads them so the flags ...
  float win_start = 0.0f;
  float win_end = -1.0f;   // sentinel: each track's own full scored length
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag(argv[i]);
    const bool wants_start = CliFlags::Matches(flag, "start") || CliFlags::Matches(flag, "s");
    const bool wants_end = CliFlags::Matches(flag, "end") || CliFlags::Matches(flag, "e");
    if (!wants_start && !wants_end) continue;
    if (i + 1 >= argc) {
      std::cerr << "[ERROR] Option '" << flag << "' needs a time in seconds (example: 12.5).\n";
      exit_code = 1;
      return true;
    }
    char* after_number = nullptr;
    const float value = std::strtof(argv[i + 1], &after_number);
    if (after_number == argv[i + 1] || *after_number != '\0') {
      std::cerr << "[ERROR] Option '" << flag << "' needs a plain number of seconds (example: 12.5).\n";
      exit_code = 1;
      return true;
    }
    if (wants_start) win_start = value; else win_end = value;
    ++i;   // the value token belongs to this flag
  }
  if (win_start < 0.0f || (win_end >= 0.0f && win_end <= win_start)) {
    std::cerr << "[ERROR] Bad play window: start " << win_start << " s, end " << win_end << " s.\n"
              << "        Rules: start >= 0; end > start when given (omit -end\n"
              << "        for the complete track). Ends clamp to the scored length.\n"
              << "        Example: aliens-invaders -play 1 -s 6 -e 30\n";
    exit_code = 1;
    return true;
  }

  // Engine/output/order options pre-pass: -output/-o <file>, -render/-r
  // and -shuffle, position-free.
  std::string export_path;
  bool shuffle_order = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag(argv[i]);
    if (CliFlags::Matches(flag, "shuffle")) {
      shuffle_order = true;
    } else if (CliFlags::Matches(flag, "output") || CliFlags::Matches(flag, "o")) {
      if (i + 1 >= argc) {
        std::cerr << "[ERROR] Option '" << flag << "' needs an output file (e.g. out.flac).\n";
        exit_code = 1;
        return true;
      }
      export_path = argv[i + 1];
      ++i;
    } else if (CliFlags::Matches(flag, "render") ||
               CliFlags::Matches(flag, "r")) {
      SoundFontSynth::SetPreferred(false);   // -render/-r: force the procedural synth
    }
  }

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);

    // Dash-agnostic matching (CliFlags): every option below accepts one or more leading dashes, so
    // -help, --help and ---help behave identically. Identity is the dash-free word; each option ...

    if (CliFlags::Matches(arg, "help") || CliFlags::Matches(arg, "h")) {
      PrintHelp();
      exit_code = 0;
      return true;
    }

    if (CliFlags::Matches(arg, "version") || CliFlags::Matches(arg, "v")) {
      PrintVersion();
      exit_code = 0;
      return true;
    }

    if (CliFlags::Matches(arg, "backends") || CliFlags::Matches(arg, "b")) {
      PrintDrivers();
      exit_code = 0;
      return true;
    }

    if (CliFlags::Matches(arg, "list") || CliFlags::Matches(arg, "l")) {
      PrintPlaylist();
      exit_code = 0;
      return true;
    }

    if (CliFlags::Matches(arg, "play") || CliFlags::Matches(arg, "p")) {
      if (i + 1 >= argc || argv[i + 1][0] == '-') {
        std::cerr << "[ERROR] -play requires at least one track number (1 to " << StageFanfare::kPieceCount << ").\n"
                  << "Examples: aliens-invaders -play 3\n"
                  << "          aliens-invaders -play 1 15\n"
                  << "          aliens-invaders -play 6,1,15,2,8\n";
        exit_code = 1;
        return true;
      }

      // Collect the consecutive non-flag tokens -- "3", "1 15", or a comma list in any spacing
      // ("6,1,15", "6, 1, 15", even "6 , 15"). The pure CliTracks::Parse normalizes the ...
      std::vector<std::string_view> tokens;
      while (i + 1 < argc && argv[i + 1][0] != '-') {
        tokens.emplace_back(argv[++i]);
      }
      const auto selection = CliTracks::Parse(tokens, StageFanfare::kPieceCount);
      if (!selection.error.empty()) {
        std::cerr << "[ERROR] Invalid track selection: " << selection.error << ".\n";
        exit_code = 1;
        return true;
      }

      if (!export_path.empty()) {
        if (selection.tracks.size() != 1) {
          std::cerr << "[ERROR] -output writes one track at a time (a single track is required).\n";
          exit_code = 1;
          return true;
        }
        if (shuffle_order) {
          std::cerr << "[ERROR] -shuffle orders playback only; -output writes a single track.\n";
          exit_code = 1;
          return true;
        }
        exit_code = RunExport(selection.tracks.front(), win_start, win_end, export_path);
        return true;
      }
      exit_code = RunJukebox(selection.tracks, win_start, win_end, shuffle_order);
      return true;
    }

    if (CliFlags::Matches(arg, "driver") || CliFlags::Matches(arg, "d")) {
      if (i + 1 >= argc) {
        std::cerr << "[ERROR] Option '" << arg << "' requires a driver name.\n"
                  << "Run 'aliens-invaders -backends' to view available graphics backends.\n";
        exit_code = 1;
        return true;
      }

      const std::string driver_name = argv[++i];
      std::vector<std::string> available;
      if (!IsDriverAvailable(driver_name, available)) {
        std::cerr << "[ERROR] Unknown or unavailable render driver '" << driver_name << "'.\n"
                  << "Available drivers on this system: ";
        for (size_t k = 0; k < available.size(); ++k) {
          if (k > 0) std::cerr << ", ";
          std::cerr << available[k];
        }
        std::cerr << "\nRun 'aliens-invaders -backends' for details.\n";
        exit_code = 1;
        return true;
      }

      Gfx::SetCustomDriverName(driver_name);
      SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, driver_name.c_str(), SDL_HINT_OVERRIDE);
      continue;
    }

    // The -log switch arms the frame-hitch flight recorder. It is consumed here so the strict
    // unknown-option guard below accepts it, and detected again in main() through the pure argv ...
    if (CliFlags::Matches(arg, CliFlags::StripDashes(GameRules::Diagnostics::kHitchLogCliFlag))) {
      continue;
    }

    // -start/-s and -end/-e were read by the pre-pass at the top of this function; consume the
    // flag-and-value pair here so the strict unknown-option guard below accepts them in any ...
    if (CliFlags::Matches(arg, "start") || CliFlags::Matches(arg, "s") ||
        CliFlags::Matches(arg, "end") || CliFlags::Matches(arg, "e")) {
      ++i;   // the value token
      continue;
    }

    // -shuffle (no value), -output/-o (value) and -render/-r were read by the pre-pass; consume
    // them here so the strict unknown-option guard accepts them in any position.
    if (CliFlags::Matches(arg, "shuffle")) {
      continue;
    }
    if (CliFlags::Matches(arg, "output") || CliFlags::Matches(arg, "o")) {
      ++i;
      continue;
    }
    if (CliFlags::Matches(arg, "render") || CliFlags::Matches(arg, "r")) {
      continue;
    }

    std::cerr << "[ERROR] Unknown command-line option: '" << arg << "'\n"
              << "Run 'aliens-invaders -help' for usage and available options.\n";
    exit_code = 1;
    return true;
  }

  if (!export_path.empty()) {
    std::cerr << "[ERROR] -output requires -play <track> "
              << "(e.g. -play 5 -s 0 -e 30 -output beethoven.flac).\n";
    exit_code = 1;
    return true;
  }
  if (shuffle_order) {
    std::cerr << "[ERROR] -shuffle requires -play <tracks> "
              << "(it randomizes the playback order).\n";
    exit_code = 1;
    return true;
  }

  return false;
}

}  // namespace CliSoundtrack

