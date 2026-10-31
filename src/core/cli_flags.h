#ifndef CLI_FLAGS_H
#define CLI_FLAGS_H

#include <cstddef>
#include <string_view>

/* Dash-agnostic command-line flag matching -- the single spelling policy for every option the game
  accepts. Why: the manual exposes exactly two forms per option (a long name and, when one ... */
namespace CliFlags {

/// Returns the dash-free identity word of a CLI token: "--log" -> "log", "-v" -> "v", "opengl" ->
/// "opengl" (a value passes through untouched), and an all-dashes or empty token -> "" (matches ...
[[nodiscard]] constexpr std::string_view StripDashes(std::string_view arg) noexcept {
  const std::size_t first_word_char = arg.find_first_not_of('-');
  if (first_word_char == std::string_view::npos) return std::string_view{};
  return arg.substr(first_word_char);
}

/// True when `arg` names the option whose dash-free identity is `dashless_name`. Accepts one or
/// more leading dashes (-x, --x, ---x, ...) and requires at least one, so bare words stay ...
[[nodiscard]] constexpr bool Matches(std::string_view arg,
                                     std::string_view dashless_name) noexcept {
  return !dashless_name.empty() && !arg.empty() && arg.front() == '-' &&
         StripDashes(arg) == dashless_name;
}

}  // namespace CliFlags

#endif  // CLI_FLAGS_H
