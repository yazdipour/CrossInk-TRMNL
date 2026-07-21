#include "KarakeepContentConverter.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {
constexpr char HTML_FIELD[] = "\"htmlContent\"";
constexpr char TEXT_FIELD[] = "\"text\"";

bool isBlockTag(const char* tag) {
  return strcmp(tag, "p") == 0 || strcmp(tag, "div") == 0 || strcmp(tag, "br") == 0 || strcmp(tag, "li") == 0 ||
         strcmp(tag, "article") == 0 || strcmp(tag, "section") == 0 || strcmp(tag, "tr") == 0 ||
         (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && tag[2] == '\0');
}

int hexDigit(const uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

const char* decodeNamedEntity(const char* entity) {
  if (strcmp(entity, "&amp;") == 0) return "&";
  if (strcmp(entity, "&lt;") == 0) return "<";
  if (strcmp(entity, "&gt;") == 0) return ">";
  if (strcmp(entity, "&quot;") == 0) return "\"";
  if (strcmp(entity, "&apos;") == 0) return "'";
  if (strcmp(entity, "&nbsp;") == 0) return " ";
  return nullptr;
}
}  // namespace

KarakeepContentConverter::KarakeepContentConverter(const Input input, const KarakeepOutput output)
    : input(input),
      output(output),
      fieldPattern(input == Input::JSON_TEXT ? TEXT_FIELD : HTML_FIELD),
      fieldPatternLen(strlen(fieldPattern)) {
  if (input == Input::HTML) {
    jsonState = JsonState::VALUE;
    found = true;
  }
}

bool KarakeepContentConverter::feed(const uint8_t* data, const size_t len) {
  if (failed) return false;
  for (size_t i = 0; i < len; ++i) {
    if (input == Input::HTML) {
      if (!feedHtml(data[i])) return false;
    } else if (!feedJson(data[i])) {
      return false;
    }
  }
  return true;
}

bool KarakeepContentConverter::finish() {
  if (input == Input::HTML && inEntity) finishEntity();
  if (pendingNewlines > 0) {
    if (!emit('\n')) return false;
    pendingNewlines = 0;
  }
  return !failed && flush() && found;
}

bool KarakeepContentConverter::feedJson(const uint8_t c) {
  switch (jsonState) {
    case JsonState::FIND_FIELD:
      if (c == static_cast<uint8_t>(fieldPattern[fieldMatch])) {
        if (++fieldMatch == fieldPatternLen) {
          jsonState = JsonState::AWAIT_COLON;
          fieldMatch = 0;
        }
      } else {
        fieldMatch = c == static_cast<uint8_t>(fieldPattern[0]) ? 1 : 0;
      }
      return true;
    case JsonState::AWAIT_COLON:
      if (isspace(c)) return true;
      if (c == ':') {
        jsonState = JsonState::AWAIT_VALUE;
        return true;
      }
      jsonState = JsonState::FIND_FIELD;
      fieldMatch = c == static_cast<uint8_t>(fieldPattern[0]) ? 1 : 0;
      return true;
    case JsonState::AWAIT_VALUE:
      if (isspace(c)) return true;
      if (c == '"') {
        found = true;
        jsonState = JsonState::VALUE;
      } else if (c == 'n') {
        jsonState = JsonState::DONE;
      }
      return true;
    case JsonState::VALUE:
      if (c == '\\') {
        jsonState = JsonState::ESCAPE;
        return true;
      }
      if (c == '"') {
        jsonState = JsonState::DONE;
        return true;
      }
      return input == Input::JSON_TEXT ? emit(c) : feedHtml(c);
    case JsonState::ESCAPE:
      jsonState = JsonState::VALUE;
      switch (c) {
        case '"':
        case '\\':
        case '/':
          return input == Input::JSON_TEXT ? emit(c) : feedHtml(c);
        case 'b':
          return true;
        case 'f':
          return true;
        case 'n':
          return input == Input::JSON_TEXT ? emit('\n') : feedHtml('\n');
        case 'r':
          return true;
        case 't':
          return input == Input::JSON_TEXT ? emit('\t') : feedHtml('\t');
        case 'u':
          unicodeValue = 0;
          unicodeDigits = 0;
          jsonState = JsonState::UNICODE;
          return true;
        default:
          failed = true;
          return false;
      }
    case JsonState::UNICODE: {
      const int digit = hexDigit(c);
      if (digit < 0) {
        failed = true;
        return false;
      }
      unicodeValue = static_cast<uint16_t>((unicodeValue << 4) | digit);
      if (++unicodeDigits < 4) return true;
      jsonState = JsonState::VALUE;
      if (unicodeValue >= 0xD800 && unicodeValue <= 0xDBFF) {
        highSurrogate = unicodeValue;
        return true;
      }
      uint32_t codepoint = unicodeValue;
      if (unicodeValue >= 0xDC00 && unicodeValue <= 0xDFFF && highSurrogate != 0) {
        codepoint = 0x10000U + ((static_cast<uint32_t>(highSurrogate) - 0xD800U) << 10) + unicodeValue - 0xDC00U;
        highSurrogate = 0;
      }
      return emitCodepoint(codepoint);
    }
    case JsonState::DONE:
      return true;
  }
  return false;
}

bool KarakeepContentConverter::feedHtml(const uint8_t c) {
  if (inTag) {
    if (c == '>') {
      finishTag();
      inTag = false;
      return true;
    }
    if (tagLen == 0 && c == '/') {
      closingTag = true;
      return true;
    }
    if (!tagNameDone && tagLen < sizeof(tagName) - 1 && (isalnum(c) || c == '-')) {
      tagName[tagLen++] = static_cast<char>(tolower(c));
      tagName[tagLen] = '\0';
    } else if (tagLen > 0) {
      tagNameDone = true;
    }
    return true;
  }

  if (c == '<') {
    inTag = true;
    closingTag = false;
    tagNameDone = false;
    tagLen = 0;
    tagName[0] = '\0';
    return true;
  }
  if (skipping) return true;

  if (inEntity) {
    if (entityLen < sizeof(entity) - 1) entity[entityLen++] = static_cast<char>(c);
    if (c == ';' || entityLen == sizeof(entity) - 1) {
      finishEntity();
    }
    return !failed;
  }
  if (c == '&') {
    inEntity = true;
    entityLen = 0;
    entity[entityLen++] = '&';
    return true;
  }
  if (isspace(c)) {
    requestSpace();
    return true;
  }

  if (pendingNewlines > 0) {
    const uint8_t count = pendingNewlines;
    pendingNewlines = 0;
    pendingSpace = false;
    for (uint8_t i = 0; i < count; ++i) {
      if (!emit('\n')) return false;
    }
  } else if (pendingSpace) {
    pendingSpace = false;
    if (!emit(' ')) return false;
  }
  return emit(c);
}

bool KarakeepContentConverter::emit(const uint8_t c) {
  if (bufferLen == sizeof(buffer) && !flush()) return false;
  buffer[bufferLen++] = c;
  hasOutput = true;
  return true;
}

bool KarakeepContentConverter::emitBytes(const uint8_t* data, const size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (!emit(data[i])) return false;
  }
  return true;
}

bool KarakeepContentConverter::emitCodepoint(const uint32_t codepoint) {
  uint8_t utf8[4];
  size_t len = 0;
  if (codepoint <= 0x7F) {
    utf8[len++] = static_cast<uint8_t>(codepoint);
  } else if (codepoint <= 0x7FF) {
    utf8[len++] = static_cast<uint8_t>(0xC0 | (codepoint >> 6));
    utf8[len++] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0xFFFF) {
    utf8[len++] = static_cast<uint8_t>(0xE0 | (codepoint >> 12));
    utf8[len++] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
    utf8[len++] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
  } else {
    utf8[len++] = static_cast<uint8_t>(0xF0 | (codepoint >> 18));
    utf8[len++] = static_cast<uint8_t>(0x80 | ((codepoint >> 12) & 0x3F));
    utf8[len++] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
    utf8[len++] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
  }
  return input == Input::JSON_HTML ? feedHtml(utf8[0]) && (len < 2 || feedHtml(utf8[1])) &&
                                         (len < 3 || feedHtml(utf8[2])) && (len < 4 || feedHtml(utf8[3]))
                                   : emitBytes(utf8, len);
}

bool KarakeepContentConverter::flush() {
  if (bufferLen == 0) return true;
  if (!output.write || !output.write(output.context, buffer, bufferLen)) {
    failed = true;
    return false;
  }
  bufferLen = 0;
  return true;
}

void KarakeepContentConverter::finishTag() {
  if (strcmp(tagName, "script") == 0 || strcmp(tagName, "style") == 0) {
    skipping = !closingTag;
  }
  if (!skipping && isBlockTag(tagName)) requestNewline();
}

void KarakeepContentConverter::finishEntity() {
  entity[entityLen] = '\0';
  inEntity = false;
  if (entity[0] == '&' && entity[1] == '#') {
    char* end = nullptr;
    const bool hex = entity[2] == 'x' || entity[2] == 'X';
    const unsigned long value = strtoul(entity + (hex ? 3 : 2), &end, hex ? 16 : 10);
    if (end && *end == ';' && value <= 0x10FFFFUL) {
      emitPendingWhitespace();
      emitCodepoint(static_cast<uint32_t>(value));
      return;
    }
  }
  if (const char* decoded = decodeNamedEntity(entity)) {
    emitPendingWhitespace();
    emitBytes(reinterpret_cast<const uint8_t*>(decoded), strlen(decoded));
  } else {
    emitPendingWhitespace();
    emitBytes(reinterpret_cast<const uint8_t*>(entity), entityLen);
  }
}

bool KarakeepContentConverter::emitPendingWhitespace() {
  if (pendingNewlines > 0) {
    const uint8_t count = pendingNewlines;
    pendingNewlines = 0;
    pendingSpace = false;
    for (uint8_t i = 0; i < count; ++i) {
      if (!emit('\n')) return false;
    }
  } else if (pendingSpace) {
    pendingSpace = false;
    return emit(' ');
  }
  return true;
}

void KarakeepContentConverter::requestSpace() {
  if (hasOutput && pendingNewlines == 0) pendingSpace = true;
}

void KarakeepContentConverter::requestNewline() {
  pendingSpace = false;
  if (hasOutput) pendingNewlines = 2;
}
