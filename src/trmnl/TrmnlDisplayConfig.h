#pragma once

#include <cstdint>

namespace trmnl {

enum class Orientation : uint8_t { Landscape = 0, Portrait = 1 };

struct DisplaySize {
  int width;
  int height;
};

constexpr DisplaySize displaySizeFor(const Orientation orientation) {
  return orientation == Orientation::Portrait ? DisplaySize{480, 800} : DisplaySize{800, 480};
}

constexpr const char* modelFor(const Orientation /*orientation*/) { return "og_png"; }

}  // namespace trmnl
