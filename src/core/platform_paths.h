#ifndef PLATFORM_PATHS_H
#define PLATFORM_PATHS_H

#include <SDL3/SDL.h>

#include <string>

/**
 * @class PlatformPaths
 * @brief Universal platform-agnostic storage abstraction using SDL3 native
 * sandboxing. Ensures zero-permission sandboxed execution across Linux,
 * Windows, macOS, iOS, and Android.
 */
class PlatformPaths {
 public:
  [[nodiscard]] static std::string GetSaveDirectory() {
    char* pref_path = SDL_GetPrefPath("ClaudioRodrigues", "AliensInvaders");
    if (pref_path) {
      std::string path(pref_path);
      SDL_free(pref_path);
      return path;
    }
    return "./";
  }

  [[nodiscard]] static std::string GetScoresFilePath() {
    return GetSaveDirectory() + "aliens-invaders.scores";
  }

  [[nodiscard]] static std::string GetPlayerIdentity() {
    const char* custom_alias = SDL_getenv("ALIENS_INVADERS_PSEUDO");
    if (custom_alias && custom_alias[0]) return std::string(custom_alias);

    const char* user = SDL_getenv("USER");
    if (!user || !user[0]) user = SDL_getenv("USERNAME");
    return (user && user[0]) ? std::string(user) : "Starfighter Pilot";
  }
};

#endif  // PLATFORM_PATHS_H
