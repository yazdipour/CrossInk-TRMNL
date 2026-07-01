#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

class TrmnlDisplayJsonParser {
 public:
  TrmnlDisplayJsonParser();
  void reset();
  void feed(const char* data, size_t len);

  bool foundImageUrl() const;
  bool hasError() const;
  const char* getImageUrl() const;
  const char* getFilename() const;
  uint32_t getRefreshRateSeconds() const;

 private:
  enum class LastKey : uint8_t { NONE, IMAGE_URL, FILENAME, REFRESH_RATE };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  StreamingJsonParser parser;
  LastKey lastKey = LastKey::NONE;
  uint16_t depth = 0;
  // Matches StreamingJsonParser::TOKEN_BUF_SIZE -- see its comment for why this needs
  // real headroom beyond a "typical" image_url length.
  char imageUrl[1024] = "";
  char filename[128] = "";
  uint32_t refreshRateSeconds = 0;
  bool imageUrlFound = false;
};
