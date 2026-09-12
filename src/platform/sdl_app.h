#ifndef SDL_APP_H
#define SDL_APP_H

namespace Platform {
bool Init();
void Quit();
void SetHighThreadPriority();
}  // namespace Platform

#endif  // SDL_APP_H
