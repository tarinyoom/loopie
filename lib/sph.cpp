#include "sph.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <random>

#include "array.tpp"
#include "bbox.hpp"
#include "color.hpp"
#include "grid.hpp"
#include "matrix.hpp"
#include "pixel.hpp"
#include "rendering.tpp"

template <std::size_t N>
using Vector = std::array<double, N>;

SPHState::SPHState(std::size_t n_particles)
    : n_particles(n_particles),
      positions(std::vector<std::array<double, 2>>(n_particles, {0.0, 0.0})),
      velocities(std::vector<std::array<double, 2>>(n_particles, {0.0, 0.0})),
      densities(std::vector<double>(n_particles, 0.0)),
      pressures(std::vector<double>(n_particles, 0.0)) {}

auto init() -> SPHState {
  SPHState state(N_PARTICLES);
  state.boundary = {{-20.0, -10.0}, {20.0, 10.0}};

  std::mt19937 gen(0);
  std::uniform_real_distribution<> p_x(-20.0, 20.0);
  std::uniform_real_distribution<> p_y(-10.0, 10.0);

  for (auto i = 0; i < state.positions.size(); i++) {
    auto x = p_x(gen);
    auto y = p_y(gen);
    state.positions[i] = {x, y};
    state.velocities[i] = {-0.5 * x, -0.5 * y};
  }
  return state;
}

auto kernel(const Vector<2>& a, const Vector<2>& b, double r,
            double scale) -> double {
  auto diff = a - b;
  auto d2 = diff * diff;
  if (d2 > r * r) {
    return 0.0;
  }

  auto c = r * r - d2;
  auto numerator = 4.0 * c * c * c * scale;
  auto denominator = std::numbers::pi * std::pow(r, 8.0);
  return numerator / denominator;
}

auto kernel_gradient(const Vector<2>& p, const Vector<2>& c, double r,
                     double scale) -> std::array<double, 2> {
  auto diff = p - c;
  auto d2 = diff * diff;
  if (d2 > r * r) {
    return {0.0, 0.0};
  }

  auto v = r * r - d2;
  auto common_numerator = 24.0 * v * v * scale;
  auto denominator = std::numbers::pi * std::pow(r, 8.0);
  auto common = common_numerator / denominator;
  return {(p[0] - c[0]) * common, (p[1] - c[1]) * common};
}

auto update_densities(SPHState& s) -> void {
  for (auto i = 0; i < s.n_particles; i++) {
    s.densities[i] = 0.0;
  }

  for (auto i = 0; i < s.positions.size(); i++) {
    for (auto j = 0; j <= i; j++) {
      auto v = kernel(s.positions[i], s.positions[j], OUTER_R, 1.0);
      if (i != j) {  // asymmetric case
        s.densities[i] += v;
        s.densities[j] += v;
      } else {  // symmetric case
        s.densities[i] += v;
      }
    }
  }
}

auto update_pressures(SPHState& s) -> void {
  auto ref_density = s.n_particles / s.boundary.volume();
  for (auto i = 0; i < s.n_particles; i++) {
    s.pressures[i] = std::pow(s.densities[i], 7.0) - std::pow(ref_density, 7.0);
    s.pressures[i] *= 100000.0;
  }
}

auto update_velocities(SPHState& s, double h) -> void {
  for (auto i = 0; i < s.n_particles; i++) {
    for (auto j = 0; j < i; j++) {
      auto grad = kernel_gradient(s.positions[i], s.positions[j], OUTER_R, 1.0);
      auto l = s.pressures[i] / (s.densities[i] * s.densities[i]);
      auto r = s.pressures[j] / (s.densities[j] * s.densities[j]);
      auto acc = h * (l + r) * grad;
      s.velocities[i] += acc;
      s.velocities[j] += -1.0 * acc;
    }
    s.velocities[i] += h * std::array<double, 2>{0.0, 2.0};
  }
}

auto step(SPHState&& pre, double h) -> SPHState {
  SPHState post(pre.n_particles);
  for (auto i = 0; i < pre.positions.size(); i++) {
    post.positions[i] = std::move(pre.positions[i]) + h * pre.velocities[i];
    post.velocities[i] = std::move(pre.velocities[i]);
    post.boundary = std::move(pre.boundary);
    update_densities(post);
    update_pressures(post);
    update_velocities(post, h);

    for (auto dim = 0; dim < 2; dim++) {
      auto& proj = post.positions[i][dim];
      if (proj < post.boundary.min[dim]) {
        proj = post.boundary.min[dim];
        post.velocities[i][dim] *= -1;
      } else if (proj > post.boundary.max[dim]) {
        proj = post.boundary.max[dim];
        post.velocities[i][dim] *= -1;
      }
      auto& v = post.velocities[i][dim];
      if (std::abs(v) > 10.0) {
        v = -1.0 * std::signbit(v) * 10.0;
      }
    }
  }
  return post;
}

static Matrix<double, 3, 3> world_to_screen{
    {{{10.0, 0.0, 320.0}, {0.0, 10.0, 240.0}, {0.0, 0.0, 1.0}}}};

static Matrix<double, 3, 3> screen_to_world{
    {{{0.1, 0.0, -32.0}, {0.0, 0.1, -24.0}, {0.0, 0.0, 1.0}}}};

auto get_light(const std::array<double, 2>& p,
               const std::array<double, 2>& center) -> Color {
  auto p_screen = homogenize(p);
  auto p_world = screen_to_world * p_screen;
  auto dehom = dehomogenize(p_world);
  return Blue * kernel(center, dehom, OUTER_R, 3.0) +
         White * kernel(center, dehom, INNER_R, 1.0);
}

auto clamp(const Color& c) -> Color {
  return {std::clamp(c.r, 0.0, 1.0), std::clamp(c.g, 0.0, 1.0),
          std::clamp(c.b, 0.0, 1.0)};
}

auto render_circle(const Vector<2>& center, const Bbox<int, 2>& bounds,
                   Grid<Color>& buffer) {
  Bbox<int, 2> clipped_bounds = bounds * buffer.bounds();
  for (auto i = clipped_bounds.min[0]; i < clipped_bounds.max[0]; i++) {
    for (auto j = clipped_bounds.min[1]; j < clipped_bounds.max[1]; j++) {
      Color c = {0.0, 0.0, 0.0};
      constexpr auto ss_d = 1.0 / static_cast<double>(MSAA_LINEAR_DENSITY);
      for (auto di = 0; di < MSAA_LINEAR_DENSITY; di++) {
        for (auto dj = 0; dj < MSAA_LINEAR_DENSITY; dj++) {
          // Subsampling at -.4, -.2, 0, .2, .4
          auto i_subsample = static_cast<double>(i) - -0.5 +
                             ss_d * (0.5 + static_cast<double>(di));
          auto j_subsample =
              static_cast<double>(j) - 0.4 + 0.2 * static_cast<double>(dj);
          Color dc = get_light(std::array<double, 2>{i_subsample, j_subsample},
                               center) *
                     (1.0 / MSAA_LINEAR_DENSITY / MSAA_LINEAR_DENSITY);
          c += clamp(dc);
        }
      }
      buffer[i][j] += c;
    }
  }
}

auto quantize(double v) -> std::uint8_t {
  auto clamped = std::clamp(v, 0.0, 1.0);
  auto rounded = std::round(clamped * 255.0);
  auto result = static_cast<std::uint8_t>(rounded);
  return result;
}

auto finalize(Grid<Color>&& buffer) -> Grid<Pixel> {
  auto [m, n] = buffer.size();
  Grid<Pixel> result(m, n, {0, 0, 0});
  for (auto i = 0; i < m; i++) {
    for (auto j = 0; j < n; j++) {
      auto& color = buffer[i][j];
      auto r = quantize(color.r);
      auto g = quantize(color.g);
      auto b = quantize(color.b);
      result[i][j] = {r, g, b};
    }
  }
  return result;
}

auto render(const SPHState& s) -> Grid<Pixel> {
  Grid<Color> buffer(640, 480, Black);

  // For each position, render a circle
  for (auto& pos : s.positions) {
    // Find bounding box in world space
    auto pos_w = pos;
    Vector<2> radius_offset = {OUTER_R, OUTER_R};
    Bbox<double, 3> bounds_w = {homogenize(pos_w - radius_offset),
                                homogenize(pos_w + radius_offset)};

    // Convert bounding box to pixel space
    auto bounds_s = bounds_w.transform(world_to_screen);
    auto bounds_p = conservative_integral_bounds(dehomogenize(bounds_s));

    // Render circle into buffer
    render_circle(pos, bounds_p, buffer);
  }

  return finalize(std::move(buffer));
}

auto build_animation() -> Animation<SPHState> { return {init, step, render}; }
