#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "TrmnlDisplayConfig.h"

class TrmnlSleepClient {
 public:
  enum class ImageKind : uint8_t { Unknown, Bmp, Png };

  struct Config {
    const char* serverUrl;
    const char* apiKey;
    const char* deviceId;
    trmnl::DisplaySize displaySize;
    const char* model;
  };

  static constexpr const char* CACHE_BMP = "/.crosspoint/trmnl_sleep.bmp";
  static constexpr const char* CACHE_PNG = "/.crosspoint/trmnl_sleep.png";

  static bool hasConfig(const Config& config);
  static bool fetchLatest(const Config& config);
  static ImageKind detectImageKind(const std::string& path);
  static bool validateImage(const std::string& path, ImageKind& kind);
  static const char* cachePathFor(ImageKind kind);

 private:
  static constexpr const char* CACHE_TMP = "/.crosspoint/trmnl_sleep.tmp";
  static constexpr uint8_t WIFI_RETRIES = 20;
  static constexpr uint16_t WIFI_RETRY_DELAY_MS = 500;
  static constexpr size_t DISPLAY_JSON_MAX_BYTES = 4096;
  static constexpr size_t IMAGE_MAX_BYTES = 256 * 1024;

  static bool connectWifi();
  static void disconnectWifi();
  static std::string resolveDeviceId(const char* configuredDeviceId);
  static bool replaceCache(const std::string& tmpPath, ImageKind kind);
};
