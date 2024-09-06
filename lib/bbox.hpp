#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <string>

#include "matrix.hpp"

namespace scarf {

template <typename T, std::size_t N>
  requires(N > 0)
struct Bbox {
  Bbox() : min(), max() {
    min.fill(std::numeric_limits<T>::min());
    max.fill(std::numeric_limits<T>::max());
  }

  Bbox(const std::array<T, N>& min, const std::array<T, N>& max)
      : min(min), max(max) {}

  auto operator*(const Bbox<T, N>& other) const -> Bbox {
    Bbox<T, N> result;
    for (auto i = 0; i < N; i++) {
      result.min[i] = std::max(min[i], other.min[i]);
      result.max[i] = std::min(max[i], other.max[i]);
    }
    return result;
  }

  auto transform(const renderer::Matrix<T, N, N>& a) const -> Bbox<T, N> {
    return Bbox<T, N>(a * min, a * max);
  }

  auto volume() -> T {
    auto vol = max[0] - min[0];  // N > 0
    for (auto i = 1; i < N; i++) {
      vol *= max[i] - min[i];
    }
    return vol;
  }

  std::array<T, N> min;
  std::array<T, N> max;
};

}  // namespace scarf
