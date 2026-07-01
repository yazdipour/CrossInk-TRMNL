#pragma once

#include <cstddef>
#include <cstdint>

struct JsonCallbacks {
  void* ctx;
  void (*onKey)(void* ctx, const char* key, size_t len);
  void (*onString)(void* ctx, const char* value, size_t len);
  void (*onNumber)(void* ctx, const char* value, size_t len);
  void (*onBool)(void* ctx, bool value);
  void (*onNull)(void* ctx);
  void (*onObjectStart)(void* ctx);
  void (*onObjectEnd)(void* ctx);
  void (*onArrayStart)(void* ctx);
  void (*onArrayEnd)(void* ctx);
};

class StreamingJsonParser {
 public:
  // \uXXXX escapes are decoded to real UTF-8 bytes (see handleStringChar()'s
  // unicodeDigitsRemaining handling), so most escaped characters cost 1-4 bytes here,
  // not the 6 raw source bytes of "&" etc. TRMNL's /api/display responses measured
  // ~486-521 bytes for image_url alone depending on how the server chose to escape it;
  // sized with real headroom for that plus future longer URLs, not tuned to one case.
  static constexpr size_t TOKEN_BUF_SIZE = 1024;
  static constexpr size_t MAX_NESTING = 32;

  explicit StreamingJsonParser(const JsonCallbacks& callbacks);

  void reset();
  void feed(const char* data, size_t len);

  bool hasError() const { return error; }

 private:
  enum class State : uint8_t {
    SCANNING,
    IN_STRING_KEY,
    IN_STRING_VALUE,
    IN_NUMBER,
    IN_LITERAL,
    SKIP_STRING,
  };

  enum class Container : uint8_t {
    NONE,
    OBJECT,
    ARRAY,
  };

  void handleScanning(char c);
  void handleStringChar(char c);
  void handleNumber(char c);
  void handleLiteral(char c);
  void handleSkipString(char c);

  void appendToken(char c);
  void emitToken();

  // \uXXXX decoding. A code unit spans up to 4 characters and can arrive split across
  // separate feed() calls, so the in-progress digits/value must be member state, not
  // locals -- mirrors how literalPos/literalExpected track a split true/false/null.
  void finishUnicodeEscape();
  void appendUtf8CodePoint(uint32_t codepoint);
  static int hexDigitValue(char c);

  bool inArray() const { return nestingDepth > 0 && nestingStack[nestingDepth - 1] == Container::ARRAY; }

  JsonCallbacks cb;
  char tokenBuf[TOKEN_BUF_SIZE];
  size_t tokenLen;
  State state;
  bool expectingValue;
  bool escaped;
  bool tokenOverflow;
  bool error;

  uint8_t unicodeDigitsRemaining;  // 0 = not mid-\uXXXX; else counts down 4..1
  uint16_t unicodeValue;           // hex digits accumulated so far for the current \uXXXX
  uint16_t pendingHighSurrogate;   // 0 = none; else a UTF-16 high surrogate awaiting its low pair

  Container nestingStack[MAX_NESTING];
  uint8_t nestingDepth;

  char literalExpected[6];
  uint8_t literalLen;
  uint8_t literalPos;
};
