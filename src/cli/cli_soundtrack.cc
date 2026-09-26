#include "cli_soundtrack.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include "constants.h"
#include "sdl_audio.h"
#include "sdl_renderer.h"
#include "stage_fanfare.h"

namespace CliSoundtrack {

namespace {

std::atomic<bool> g_interrupted{false};

void SignalHandler(int) {
  g_interrupted.store(true, std::memory_order_relaxed);
}

void PrintHelp() {
  std::cout << "\033[1m==============================================================================\033[0m\n"
            << " \033[1;36mALIENS INVADERS — COMMAND LINE REFERENCE\033[0m\n"
            << "\033[1m==============================================================================\033[0m\n"
            << "  \033[1mUsage:\033[0m\n"
            << "    aliens-invaders                       Launch game (Default: Vulkan with OpenGL fallback)\n"
            << "    aliens-invaders -driver <name>        Launch game with specified graphics driver\n"
            << "    aliens-invaders -drivers              List all available 2D graphics rendering drivers\n"
            << "    aliens-invaders -playlist             List all 15 classical soundtrack movements\n"
            << "    aliens-invaders -play <n>             Play track <n> (1 to 15) in terminal\n"
            << "    aliens-invaders -play <a> <b>         Play sequence from <a> to <b> inclusive (b >= a)\n"
            << "    aliens-invaders -help                 Display this command-line manual\n\n"
            << "  \033[1mAliases:\033[0m\n"
            << "    -driver, --driver, -d <name>          (e.g. aliens-invaders -driver opengl)\n"
            << "    -drivers, --drivers\n"
            << "    -playlist, --playlist, -list, --list, -l\n"
            << "    -play, --play, -p\n"
            << "    -help, --help, -h\n"
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
            << " \033[1;36mALIENS INVADERS — AVAILABLE 2D GRAPHICS RENDER DRIVERS (SDL3)\033[0m\n"
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
            << " \033[1;36mALIENS INVADERS — SYMPHONIC SOUNDTRACK (5-INSTRUMENT CLASSICAL QUINTET ANTHOLOGY)\033[0m\n"
            << "\033[1m===================================================================================================\033[0m\n"
            << "  CD Quality Stereo 2.1 | In-phase Sub-bass (< 85 Hz) | Zero Digital Clipping\n"
            << "---------------------------------------------------------------------------------------------------\n";

  for (int stage = 1; stage <= StageFanfare::kPieceCount; ++stage) {
    const auto& m = StageFanfare::GetMetadata(stage);
    std::cout << " \033[1;33m[" << std::setw(2) << stage << "]\033[0m "
              << "\033[1m" << m.composer << "\033[0m (" << m.nationality << ", " << m.life_dates << ")\n"
              << "      \033[1;32m" << m.title << " (" << m.opus_catalog << ")\033[0m — " << m.movement_phrase << "\n"
              << "      \033[0;90m“" << m.historical_curiosity << "”\033[0m\n\n";
  }

  std::cout << "---------------------------------------------------------------------------------------------------\n"
            << "  \033[1mTo listen from terminal:\033[0m aliens-invaders -play <track_number> (e.g. aliens-invaders -play 1 4)\n"
            << "\033[1m===================================================================================================\033[0m\n";
}

bool PlayTrack(SoundManager& audio, int track) {
  if (track < 1 || track > StageFanfare::kPieceCount) return false;
  const auto& m = StageFanfare::GetMetadata(track);

  std::cout << "\n\033[1m------------------------------------------------------------------------------\033[0m\n"
            << " \033[1;36mNOW PLAYING: Track [" << track << "/" << StageFanfare::kPieceCount << "]\033[0m\n"
            << "   • \033[1mComposer:\033[0m   " << m.composer << " (" << m.nationality << ", " << m.life_dates << ")\n"
            << "   • \033[1mWork:\033[0m       " << m.title << " (" << m.opus_catalog << ")\n"
            << "   • \033[1mMovement:\033[0m   " << m.movement_phrase << "\n"
            << "   • \033[1mNote:\033[0m       \033[0;90m“" << m.historical_curiosity << "”\033[0m\n"
            << "\033[1m------------------------------------------------------------------------------\033[0m\n";

  // Headless jukebox: no frame budget, so a blocking bake is correct.
  // In the graphical game this call is forbidden (see PlayStageFanfare).
  audio.BakeFanfare(track);
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

int RunJukebox(int start_track, int end_track) {
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

    std::cout << "\033[1m==============================================================================\033[0m\n"
              << " \033[1;32mALIENS INVADERS — HEADLESS TERMINAL JUKEBOX (Tracks " << start_track << " to " << end_track << ")\033[0m\n"
              << "\033[1m==============================================================================\033[0m\n";

    for (int t = start_track; t <= end_track; ++t) {
      if (g_interrupted.load(std::memory_order_relaxed)) break;
      const bool completed = PlayTrack(audio, t);
      if (!completed) break;

      if (t < end_track && !g_interrupted.load(std::memory_order_relaxed)) {
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

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);

    if (arg == "-help" || arg == "--help" || arg == "-h") {
      PrintHelp();
      exit_code = 0;
      return true;
    }

    if (arg == "-drivers" || arg == "--drivers") {
      PrintDrivers();
      exit_code = 0;
      return true;
    }

    if (arg == "-playlist" || arg == "--playlist" || arg == "-list" || arg == "--list"|| arg == "-l" ) {
      PrintPlaylist();
      exit_code = 0;
      return true;
    }

    if (arg == "-play" || arg == "--play" || arg == "-p") {
      if (i + 1 >= argc) {
        std::cerr << "[ERROR] -play requires at least one track number (1 to " << StageFanfare::kPieceCount << ").\n"
                  << "Example: aliens-invaders -play 1\n"
                  << "         aliens-invaders -play 1 4\n";
        exit_code = 1;
        return true;
      }

      const int track_a = std::atoi(argv[i + 1]);
      int track_b = track_a;

      if (i + 2 < argc && argv[i + 2][0] != '-') {
        track_b = std::atoi(argv[i + 2]);
      }

      if (track_a < 1 || track_a > StageFanfare::kPieceCount ||
          track_b < 1 || track_b > StageFanfare::kPieceCount ||
          track_b < track_a) {
        std::cerr << "[ERROR] Invalid track selection: [" << argv[i + 1];
        if (track_b != track_a) std::cerr << ", " << track_b;
        std::cerr << "]. Tracks must be in range 1 to " << StageFanfare::kPieceCount << ", with second >= first.\n";
        exit_code = 1;
        return true;
      }

      exit_code = RunJukebox(track_a, track_b);
      return true;
    }

    if (arg == "-driver" || arg == "--driver" || arg == "-d") {
      if (i + 1 >= argc) {
        std::cerr << "[ERROR] Option '" << arg << "' requires a driver name.\n"
                  << "Run 'aliens-invaders -drivers' to view available graphics drivers.\n";
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
        std::cerr << "\nRun 'aliens-invaders -drivers' for details.\n";
        exit_code = 1;
        return true;
      }

      Gfx::SetCustomDriverName(driver_name);
      SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, driver_name.c_str(), SDL_HINT_OVERRIDE);
      continue;
    }

    std::cerr << "[ERROR] Unknown command-line option: '" << arg << "'\n"
              << "Run 'aliens-invaders -help' for usage and available options.\n";
    exit_code = 1;
    return true;
  }

  return false;
}

}  // namespace CliSoundtrack
