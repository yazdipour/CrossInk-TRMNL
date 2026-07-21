#pragma once

#include <cstddef>
#include <cstdint>

struct KarakeepOutput {
  void* context;
  bool (*write)(void* context, const uint8_t* data, size_t len);
};

class KarakeepContentConverter {
 public:
  enum class Input : uint8_t { HTML, JSON_HTML, JSON_TEXT };

  KarakeepContentConverter(Input input, KarakeepOutput output);
  bool feed(const uint8_t* data, size_t len);
  bool finish();
  bool foundContent() const { return found; }

 private:
  enum class JsonState : uint8_t { FIND_FIELD, AWAIT_COLON, AWAIT_VALUE, VALUE, ESCAPE, UNICODE, DONE };

  bool feedJson(uint8_t c);
  bool feedHtml(uint8_t c);
  bool emit(uint8_t c);
  bool emitBytes(const uint8_t* data, size_t len);
  bool emitCodepoint(uint32_t codepoint);
  bool flush();
  void finishTag();
  void finishEntity();
  bool emitPendingWhitespace();
  void requestSpace();
  void requestNewline();

  Input input;
  KarakeepOutput output;
  JsonState jsonState = JsonState::FIND_FIELD;
  const char* fieldPattern;
  size_t fieldPatternLen;
  size_t fieldMatch = 0;
  uint16_t unicodeValue = 0;
  uint8_t unicodeDigits = 0;
  uint16_t highSurrogate = 0;

  bool inTag = false;
  bool closingTag = false;
  bool tagNameDone = false;
  bool inEntity = false;
  bool skipping = false;
  bool found = false;
  bool failed = false;
  bool pendingSpace = false;
  bool hasOutput = false;
  uint8_t pendingNewlines = 0;
  char tagName[16] = "";
  size_t tagLen = 0;
  char entity[16] = "";
  size_t entityLen = 0;
  uint8_t buffer[128] = {};
  size_t bufferLen = 0;
};
