#ifndef PLATFORM_PATHS_H
#define PLATFORM_PATHS_H

#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

class PlatformPaths {
 public:
  PlatformPaths() = delete;

  [[nodiscard]] static std::string GetSaveDirectory() {
    char* pref_path = SDL_GetPrefPath(nullptr, "aliens-invaders");
    if (pref_path && pref_path[0] != '\0') {
      std::string path(pref_path);
      SDL_free(pref_path);
      EnsureDirectoryExists(path);
      return path;
    }
    if (pref_path) {
      SDL_free(pref_path);
    }

    const char* xdg_data = SDL_getenv("XDG_DATA_HOME");
    if (xdg_data && xdg_data[0] != '\0') {
      std::string path = std::string(xdg_data) + "/aliens-invaders/";
      EnsureDirectoryExists(path);
      return path;
    }

    const char* home = SDL_getenv("HOME");
    if (home && home[0] != '\0') {
      std::string path = std::string(home) + "/.local/share/aliens-invaders/";
      EnsureDirectoryExists(path);
      return path;
    }

    const char* appdata = SDL_getenv("APPDATA");
    if (appdata && appdata[0] != '\0') {
      std::string path = std::string(appdata) + "\\aliens-invaders\\";
      EnsureDirectoryExists(path);
      return path;
    }

    const char* base_path = SDL_GetBasePath();
    if (base_path && base_path[0] != '\0') {
      return std::string(base_path);
    }

    return "./";
  }

  [[nodiscard]] static std::string GetScoresFilePath() {
    std::error_code ec;

#if defined(__linux__) || defined(__FreeBSD__)
    constexpr const char* kSystemSharedScores = "/var/games/aliens-invaders.scores";
    if (fs::exists(kSystemSharedScores, ec)) {
      std::ofstream test_out(kSystemSharedScores, std::ios::app);
      if (test_out.is_open()) {
        return kSystemSharedScores;
      }
    }
#endif

    const std::string standard_path = GetSaveDirectory() + "aliens-invaders.scores";

    if (!fs::exists(standard_path, ec) && fs::exists("./aliens-invaders.scores", ec)) {
      EnsureDirectoryExists(fs::path(standard_path).parent_path().string());
      fs::copy_file("./aliens-invaders.scores", standard_path, fs::copy_options::overwrite_existing, ec);
    }

    return standard_path;
  }

  [[nodiscard]] static std::string GetPlayerIdentity() {
    const char* custom_alias = SDL_getenv("ALIENS_INVADERS_PLAYER");
    if (!custom_alias || custom_alias[0] == '\0') {
      custom_alias = SDL_getenv("ALIENS_INVADERS_PSEUDO");
    }
    if (custom_alias && custom_alias[0] != '\0') {
      return SanitizeCallsign(custom_alias);
    }

    const char* steam_user = SDL_getenv("SteamPersonName");
    if (steam_user && steam_user[0] != '\0') {
      return SanitizeCallsign(steam_user);
    }

    const char* user = SDL_getenv("USER");
    if (!user || user[0] == '\0') user = SDL_getenv("LOGNAME");
    if (!user || user[0] == '\0') user = SDL_getenv("USERNAME");

    if (user && user[0] != '\0') {
      std::string sanitized = SanitizeCallsign(user);
      if (sanitized.rfind("u0_a", 0) != 0 &&
          sanitized.rfind("app_", 0) != 0 &&
          sanitized != "root" &&
          sanitized != "default" &&
          sanitized != "mobile") {
        return sanitized;
      }
    }

    return "Starfighter Pilot";
  }

 private:
  static void EnsureDirectoryExists(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
  }

  [[nodiscard]] static std::string SanitizeCallsign(std::string name) {
    std::erase_if(name, [](unsigned char c) { return c < 32 || c > 126; });
    while (!name.empty() && name.front() == ' ') name.erase(0, 1);
    while (!name.empty() && name.back() == ' ') name.pop_back();

    if (name.empty()) {
      return "Starfighter Pilot";
    }

    if (name.length() > 20) {
      name.resize(20);
    }

    if (name[0] >= 'a' && name[0] <= 'z') {
      name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    }

    return name;
  }
};

#endif  // PLATFORM_PATHS_H
