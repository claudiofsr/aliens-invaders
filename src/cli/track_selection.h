#ifndef CLI_TRACK_SELECTION_H
#define CLI_TRACK_SELECTION_H

#include <string>
#include <string_view>
#include <vector>

/* Pure track-selection parser for the jukebox CLI -- no SDL, no I/O, no globals, so the headless
  unit tests exercise the exact production logic (dependency inversion, same recipe as the ... */
namespace CliTracks {

struct Selection {
  std::vector<int> tracks;  ///< playback order (ranges expanded ascending)
  std::string error;        ///< empty on success; didactic reason otherwise
};

/// Parses the collected -play tokens against the catalogue size max_track.
[[nodiscard]] inline Selection Parse(const std::vector<std::string_view>& tokens,
                                     int max_track) {
  Selection out;

  auto bad = [&](std::string reason) {
    out.tracks.clear();
    out.error = std::move(reason);
    return out;
  };

  if (tokens.empty()) {
    return bad("no track numbers given");
  }

  // Strict digits-only number parse; overflow-safe via early magnitude cap.
  auto parse_number = [&](std::string_view text, int& value) -> bool {
    if (text.empty()) return false;
    long long accumulated = 0;
    for (const char c : text) {
      if (c < '0' || c > '9') return false;
      accumulated = accumulated * 10 + (c - '0');
      if (accumulated > static_cast<long long>(max_track)) {
        value = static_cast<int>(accumulated);  // out-of-range, quoted below
        return true;
      }
    }
    value = static_cast<int>(accumulated);
    return true;
  };

  auto push_track = [&](std::string_view piece) -> bool {
    // Trim surrounding spaces ("6, 1" quoted as one token).
    while (!piece.empty() && piece.front() == ' ') piece.remove_prefix(1);
    while (!piece.empty() && piece.back() == ' ') piece.remove_suffix(1);
    int value = 0;
    if (!parse_number(piece, value)) {
      out.error = "not a track number: '" + std::string(piece) + "'";
      return false;
    }
    if (value < 1 || value > max_track) {
      out.error = "track " + std::string(piece) + " out of range (1 to " +
                  std::to_string(max_track) + ")";
      return false;
    }
    out.tracks.push_back(value);
    return true;
  };

  bool has_comma = false;
  for (const std::string_view token : tokens) {
    if (token.find(',') != std::string_view::npos) {
      has_comma = true;
      break;
    }
  }

  if (has_comma) {
    // List mode: split every token on commas; empty pieces (loose or
    // duplicated separators) are skipped, order and repeats are preserved.
    for (const std::string_view token : tokens) {
      std::size_t start = 0;
      while (true) {
        const std::size_t comma = token.find(',', start);
        const std::string_view piece = token.substr(
            start, comma == std::string_view::npos ? std::string_view::npos
                                                   : comma - start);
        if (!piece.empty() && piece.find_first_not_of(' ') != std::string_view::npos) {
          if (!push_track(piece)) return bad(out.error);
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
      }
    }
    if (out.tracks.empty()) return bad("empty track list");
    return out;
  }

  // Classic mode: one number is a single track; two numbers are an inclusive ascending range. More
  // than two without commas is almost certainly an intended list -- say so instead of guessing.
  if (tokens.size() > 2) {
    return bad("ranges take exactly two numbers; use commas for a custom "
               "order (e.g. -play 6,1,15)");
  }
  for (const std::string_view token : tokens) {
    if (!push_track(token)) return bad(out.error);
  }
  if (out.tracks.size() == 2 && out.tracks[1] < out.tracks[0]) {
    return bad("range requires the second number >= the first (" +
               std::to_string(out.tracks[0]) + ".." + std::to_string(out.tracks[1]) +
               "); for a custom order use a comma list (e.g. -play " +
               std::to_string(out.tracks[0]) + "," + std::to_string(out.tracks[1]) + ")");
  }
  if (out.tracks.size() == 2) {
    const int first = out.tracks[0];
    const int last = out.tracks[1];
    out.tracks.clear();
    for (int t = first; t <= last; ++t) out.tracks.push_back(t);
  }
  return out;
}

}  // namespace CliTracks

#endif  // CLI_TRACK_SELECTION_H
