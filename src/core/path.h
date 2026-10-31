#ifndef PATH_H
#define PATH_H

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <span>
#include <vector>

#include "math_types.h"
#include "random_stream.h"

struct FlightPath {
  std::vector<Vec2f> points;

  FlightPath() = default;

  explicit FlightPath(const std::vector<Vec2f>& raw_pts) {
    points = GenerateSmoothSplineF(raw_pts);
  }

  FlightPath(std::initializer_list<Vec2f> raw_pts) {
    points = GenerateSmoothSplineF(std::vector<Vec2f>(raw_pts));
  }

  [[nodiscard]] bool Empty() const noexcept { return points.empty(); }
  [[nodiscard]] size_t Size() const noexcept { return points.size(); }
  [[nodiscard]] const Vec2f& operator[](size_t idx) const noexcept {
    return points[idx];
  }

  /** @brief Barry-Goldman Centripetal Catmull-Rom Formulation (alpha = 0.5). Mathematically proven
   * to eliminate cusps, self-intersections, and velocity whip when control point chord lengths ... */
  static Vec2f EvaluateCentripetalCR(const Vec2f& p0, const Vec2f& p1,
                                     const Vec2f& p2, const Vec2f& p3,
                                     float t) noexcept {
    auto GetKnot = [](const Vec2f& a, const Vec2f& b) -> float {
      const float d2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
      return std::sqrt(
          std::max(1e-4f, std::sqrt(d2)));  // alpha = 0.5 (centripetal)
    };

    const float t0 = 0.0f;
    const float t1 = t0 + GetKnot(p0, p1);
    const float t2 = t1 + GetKnot(p1, p2);
    const float t3 = t2 + GetKnot(p2, p3);

    const float time = t1 + t * (t2 - t1);

    auto Interp = [](const Vec2f& a, const Vec2f& b, float ta, float tb,
                     float cur) -> Vec2f {
      if (std::abs(tb - ta) < 1e-5f) return a;
      const float factor = (cur - ta) / (tb - ta);
      return Vec2f(a.x + (b.x - a.x) * factor, a.y + (b.y - a.y) * factor);
    };

    const Vec2f a1 = Interp(p0, p1, t0, t1, time);
    const Vec2f a2 = Interp(p1, p2, t1, t2, time);
    const Vec2f a3 = Interp(p2, p3, t2, t3, time);

    const Vec2f b1 = Interp(a1, a2, t0, t2, time);
    const Vec2f b2 = Interp(a2, a3, t1, t3, time);

    return Interp(b1, b2, t1, t2, time);
  }

  /// Out-parameter core of the Catmull-Rom resampler (AGENT 4 / AGENT 7). Why: the mid-match
  /// callers (Trajectory::BuildAttack / BuildKamikazeDive) used to allocate up to three heap ...
  static void GenerateSmoothSplineInto(std::span<const Vec2f> control_pts,
                                       std::vector<Vec2f>& out) {
    const size_t n = control_pts.size();
    if (n < 2) {
      out.assign(control_pts.begin(), control_pts.end());
      return;
    }

    static thread_local std::vector<Vec2f> fine_pts;
    constexpr int kFineSteps = 36;
    fine_pts.clear();
    fine_pts.reserve((n - 1) * kFineSteps + 2);

    for (size_t i = 0; i < n - 1; ++i) {
      const auto& P1 = control_pts[i];
      const auto& P2 = control_pts[i + 1];
      const auto& P0 = (i == 0) ? P1 : control_pts[i - 1];
      const auto& P3 = (i + 2 < n) ? control_pts[i + 2] : P2;

      for (int s = 0; s < kFineSteps; ++s) {
        const float t = static_cast<float>(s) / static_cast<float>(kFineSteps);
        fine_pts.push_back(EvaluateCentripetalCR(P0, P1, P2, P3, t));
      }
    }
    fine_pts.push_back(control_pts.back());

    // Equidistant resampling keeps velocity stable and reuses the output
    // capacity, so the steady-state path is allocation-free.
    out.clear();
    out.reserve(fine_pts.size() + 16);
    out.push_back(fine_pts.front());

    constexpr float kTargetStep = 3.0f;
    float accumulated = 0.0f;

    for (size_t i = 1; i < fine_pts.size(); ++i) {
      const Vec2f p_prev = fine_pts[i - 1];
      const Vec2f p_curr = fine_pts[i];
      const float seg_len = (p_curr - p_prev).Length();

      if (seg_len <= 1e-4f) continue;

      accumulated += seg_len;
      while (accumulated >= kTargetStep) {
        const float overshoot = accumulated - kTargetStep;
        const float alpha = (seg_len - overshoot) / seg_len;
        const Vec2f interp =
            p_prev + (p_curr - p_prev) * std::clamp(alpha, 0.0f, 1.0f);
        out.push_back(interp);
        accumulated -= kTargetStep;
      }
    }

    if ((out.back() - fine_pts.back()).Length() > 0.5f) {
      out.push_back(fine_pts.back());
    }
  }

  static std::vector<Vec2f> GenerateSmoothSplineF(
      const std::vector<Vec2f>& control_pts) {
    // Thin value-semantics wrapper for startup/test callers that prefer it;
    // the out-parameter core above is the single implementation (Deep DRY).
    std::vector<Vec2f> out;
    GenerateSmoothSplineInto(control_pts, out);
    return out;
  }

  static FlightPath Loop(float start_x, float start_y, int repetitions = 2) {
    std::vector<Vec2f> raw_pts;
    raw_pts.emplace_back(start_x, start_y);
    raw_pts.emplace_back(start_x + (512.0f - start_x) * 0.5f, start_y);

    constexpr float cx = 512.0f;
    constexpr float cy = 516.0f;
    constexpr float rx = 130.0f;
    constexpr float ry = 125.0f;
    constexpr int kSteps = 36;

    for (int r = 0; r < repetitions; ++r) {
      for (int s = 0; s < kSteps; ++s) {
        const float angle =
            (static_cast<float>(s) / static_cast<float>(kSteps)) * (2.0f * Pi) -
            (Pi * 0.5f);
        const float px = cx + rx * std::cos(angle);
        const float py = cy + ry * std::sin(angle);
        raw_pts.emplace_back(px, py);
      }
    }
    return FlightPath(raw_pts);
  }

};

#endif  // PATH_H
