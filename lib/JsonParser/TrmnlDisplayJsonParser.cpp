#include "TrmnlDisplayJsonParser.h"

#include <cstdlib>
#include <cstring>

namespace {
void safeCopy(char* dst, const size_t dstSize, const char* src, const size_t srcLen) {
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}
}  // namespace

TrmnlDisplayJsonParser::TrmnlDisplayJsonParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  reset();
}

void TrmnlDisplayJsonParser::reset() {
  parser.reset();
  lastKey = LastKey::NONE;
  depth = 0;
  imageUrl[0] = '\0';
  filename[0] = '\0';
  refreshRateSeconds = 0;
  imageUrlFound = false;
}

void TrmnlDisplayJsonParser::feed(const char* data, const size_t len) { parser.feed(data, len); }
bool TrmnlDisplayJsonParser::foundImageUrl() const { return imageUrlFound; }
bool TrmnlDisplayJsonParser::hasError() const { return parser.hasError(); }
const char* TrmnlDisplayJsonParser::getImageUrl() const { return imageUrl; }
const char* TrmnlDisplayJsonParser::getFilename() const { return filename; }
uint32_t TrmnlDisplayJsonParser::getRefreshRateSeconds() const { return refreshRateSeconds; }

void TrmnlDisplayJsonParser::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<TrmnlDisplayJsonParser*>(ctx);
  if (self->depth != 1) {
    self->lastKey = LastKey::NONE;
    return;
  }
  if (len == 9 && memcmp(key, "image_url", 9) == 0) {
    self->lastKey = LastKey::IMAGE_URL;
  } else if ((len == 8 && memcmp(key, "filename", 8) == 0) || (len == 10 && memcmp(key, "image_name", 10) == 0)) {
    self->lastKey = LastKey::FILENAME;
  } else if (len == 12 && memcmp(key, "refresh_rate", 12) == 0) {
    self->lastKey = LastKey::REFRESH_RATE;
  } else {
    self->lastKey = LastKey::NONE;
  }
}

void TrmnlDisplayJsonParser::sOnString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<TrmnlDisplayJsonParser*>(ctx);
  if (self->depth == 1 && self->lastKey == LastKey::IMAGE_URL) {
    safeCopy(self->imageUrl, sizeof(self->imageUrl), value, len);
    self->imageUrlFound = self->imageUrl[0] != '\0';
  } else if (self->depth == 1 && self->lastKey == LastKey::FILENAME) {
    safeCopy(self->filename, sizeof(self->filename), value, len);
  }
  self->lastKey = LastKey::NONE;
}

void TrmnlDisplayJsonParser::sOnNumber(void* ctx, const char* value, const size_t /*len*/) {
  auto* self = static_cast<TrmnlDisplayJsonParser*>(ctx);
  if (self->depth == 1 && self->lastKey == LastKey::REFRESH_RATE) {
    self->refreshRateSeconds = static_cast<uint32_t>(strtoul(value, nullptr, 10));
  }
  self->lastKey = LastKey::NONE;
}

void TrmnlDisplayJsonParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<TrmnlDisplayJsonParser*>(ctx)->lastKey = LastKey::NONE;
}
void TrmnlDisplayJsonParser::sOnNull(void* ctx) { static_cast<TrmnlDisplayJsonParser*>(ctx)->lastKey = LastKey::NONE; }
void TrmnlDisplayJsonParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<TrmnlDisplayJsonParser*>(ctx);
  self->depth++;
  self->lastKey = LastKey::NONE;
}
void TrmnlDisplayJsonParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<TrmnlDisplayJsonParser*>(ctx);
  if (self->depth > 0) self->depth--;
  self->lastKey = LastKey::NONE;
}
void TrmnlDisplayJsonParser::sOnArrayStart(void* ctx) { sOnObjectStart(ctx); }
void TrmnlDisplayJsonParser::sOnArrayEnd(void* ctx) { sOnObjectEnd(ctx); }
