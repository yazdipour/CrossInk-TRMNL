#include "StreamingJsonParser.h"

#include <cstring>

StreamingJsonParser::StreamingJsonParser(const JsonCallbacks& callbacks) : cb(callbacks) { reset(); }

void StreamingJsonParser::reset() {
  tokenLen = 0;
  state = State::SCANNING;
  expectingValue = false;
  escaped = false;
  tokenOverflow = false;
  error = false;
  nestingDepth = 0;
  literalLen = 0;
  literalPos = 0;
  unicodeDigitsRemaining = 0;
  unicodeValue = 0;
  pendingHighSurrogate = 0;
}

void StreamingJsonParser::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len && !error; ++i) {
    char c = data[i];
    switch (state) {
      case State::SCANNING:
        handleScanning(c);
        break;
      case State::IN_STRING_KEY:
      case State::IN_STRING_VALUE:
        handleStringChar(c);
        break;
      case State::IN_NUMBER:
        handleNumber(c);
        break;
      case State::IN_LITERAL:
        handleLiteral(c);
        break;
      case State::SKIP_STRING:
        handleSkipString(c);
        break;
    }
  }
}

void StreamingJsonParser::handleScanning(char c) {
  switch (c) {
    case '"':
      tokenLen = 0;
      tokenOverflow = false;
      unicodeDigitsRemaining = 0;
      pendingHighSurrogate = 0;
      if (expectingValue || inArray()) {
        state = State::IN_STRING_VALUE;
      } else {
        state = State::IN_STRING_KEY;
      }
      break;
    case '{':
      if (nestingDepth < MAX_NESTING) {
        nestingStack[nestingDepth++] = Container::OBJECT;
      } else {
        error = true;
        return;
      }
      if (cb.onObjectStart) cb.onObjectStart(cb.ctx);
      expectingValue = false;
      break;
    case '}':
      if (cb.onObjectEnd) cb.onObjectEnd(cb.ctx);
      if (nestingDepth > 0) --nestingDepth;
      expectingValue = false;
      break;
    case '[':
      if (nestingDepth < MAX_NESTING) {
        nestingStack[nestingDepth++] = Container::ARRAY;
      } else {
        error = true;
        return;
      }
      if (cb.onArrayStart) cb.onArrayStart(cb.ctx);
      expectingValue = false;
      break;
    case ']':
      if (cb.onArrayEnd) cb.onArrayEnd(cb.ctx);
      if (nestingDepth > 0) --nestingDepth;
      expectingValue = false;
      break;
    case ':':
      expectingValue = true;
      break;
    case ',':
      expectingValue = false;
      break;
    case 't':
      if (expectingValue || inArray()) {
        memcpy(literalExpected, "true", 4);
        literalLen = 4;
        literalPos = 1;
        state = State::IN_LITERAL;
      }
      break;
    case 'f':
      if (expectingValue || inArray()) {
        memcpy(literalExpected, "false", 5);
        literalLen = 5;
        literalPos = 1;
        state = State::IN_LITERAL;
      }
      break;
    case 'n':
      if (expectingValue || inArray()) {
        memcpy(literalExpected, "null", 4);
        literalLen = 4;
        literalPos = 1;
        state = State::IN_LITERAL;
      }
      break;
    default:
      if ((expectingValue || inArray()) && (c == '-' || (c >= '0' && c <= '9'))) {
        tokenLen = 0;
        tokenOverflow = false;
        appendToken(c);
        state = State::IN_NUMBER;
      }
      break;
  }
}

void StreamingJsonParser::handleStringChar(char c) {
  if (unicodeDigitsRemaining > 0) {
    const int digit = hexDigitValue(c);
    if (digit < 0) {
      // error is terminal in normal use (feed()'s loop stops on it), but reset the
      // in-progress escape state too so the parser stays internally consistent for
      // any caller that inspects/reuses it after an error rather than discarding it.
      error = true;
      unicodeDigitsRemaining = 0;
      unicodeValue = 0;
      return;
    }
    unicodeValue = static_cast<uint16_t>((unicodeValue << 4) | static_cast<uint16_t>(digit));
    --unicodeDigitsRemaining;
    if (unicodeDigitsRemaining == 0) {
      finishUnicodeEscape();
    }
    return;
  }

  if (escaped) {
    escaped = false;
    switch (c) {
      case '"':
      case '\\':
      case '/':
        appendToken(c);
        break;
      case 'b':
        appendToken('\b');
        break;
      case 'f':
        appendToken('\f');
        break;
      case 'n':
        appendToken('\n');
        break;
      case 'r':
        appendToken('\r');
        break;
      case 't':
        appendToken('\t');
        break;
      case 'u':
        unicodeDigitsRemaining = 4;
        unicodeValue = 0;
        break;
      default:
        appendToken('\\');
        appendToken(c);
        break;
    }
    return;
  }

  if (c == '\\') {
    escaped = true;
    return;
  }

  if (c == '"') {
    if (pendingHighSurrogate != 0) {
      // String closed with an unpaired UTF-16 high surrogate -- malformed input.
      error = true;
      return;
    }
    emitToken();
    return;
  }

  appendToken(c);
}

void StreamingJsonParser::finishUnicodeEscape() {
  if (pendingHighSurrogate != 0) {
    if (unicodeValue >= 0xDC00 && unicodeValue <= 0xDFFF) {
      // Valid low surrogate: combine per the UTF-16 surrogate pair formula.
      const uint32_t codepoint = 0x10000 + ((static_cast<uint32_t>(pendingHighSurrogate) - 0xD800) << 10) +
                                 (static_cast<uint32_t>(unicodeValue) - 0xDC00);
      pendingHighSurrogate = 0;
      appendUtf8CodePoint(codepoint);
    } else {
      // High surrogate not followed by a valid low surrogate -- malformed input.
      error = true;
    }
    return;
  }

  if (unicodeValue >= 0xD800 && unicodeValue <= 0xDBFF) {
    // High surrogate: hold it and wait for the low surrogate that must follow.
    pendingHighSurrogate = unicodeValue;
    return;
  }

  if (unicodeValue >= 0xDC00 && unicodeValue <= 0xDFFF) {
    // Low surrogate with no preceding high surrogate -- malformed input.
    error = true;
    return;
  }

  appendUtf8CodePoint(unicodeValue);
}

void StreamingJsonParser::appendUtf8CodePoint(const uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    appendToken(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    appendToken(static_cast<char>(0xC0 | (codepoint >> 6)));
    appendToken(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    appendToken(static_cast<char>(0xE0 | (codepoint >> 12)));
    appendToken(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    appendToken(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    appendToken(static_cast<char>(0xF0 | (codepoint >> 18)));
    appendToken(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    appendToken(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    appendToken(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

int StreamingJsonParser::hexDigitValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void StreamingJsonParser::handleNumber(char c) {
  if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E') {
    appendToken(c);
    return;
  }

  if (!tokenOverflow && cb.onNumber) {
    tokenBuf[tokenLen] = '\0';
    cb.onNumber(cb.ctx, tokenBuf, tokenLen);
  }
  state = State::SCANNING;
  expectingValue = false;

  handleScanning(c);
}

void StreamingJsonParser::handleLiteral(char c) {
  if (c == literalExpected[literalPos]) {
    ++literalPos;
    if (literalPos == literalLen) {
      if (literalExpected[0] == 't') {
        if (cb.onBool) cb.onBool(cb.ctx, true);
      } else if (literalExpected[0] == 'f') {
        if (cb.onBool) cb.onBool(cb.ctx, false);
      } else {
        if (cb.onNull) cb.onNull(cb.ctx);
      }
      state = State::SCANNING;
      expectingValue = false;
    }
  } else {
    error = true;
  }
}

void StreamingJsonParser::handleSkipString(char c) {
  if (escaped) {
    escaped = false;
    return;
  }
  if (c == '\\') {
    escaped = true;
    return;
  }
  if (c == '"') {
    state = State::SCANNING;
    expectingValue = false;
  }
}

void StreamingJsonParser::appendToken(char c) {
  if (tokenLen < TOKEN_BUF_SIZE - 1) {
    tokenBuf[tokenLen++] = c;
  } else {
    tokenOverflow = true;
  }
}

void StreamingJsonParser::emitToken() {
  if (state == State::IN_STRING_KEY) {
    if (!tokenOverflow && cb.onKey) {
      tokenBuf[tokenLen] = '\0';
      cb.onKey(cb.ctx, tokenBuf, tokenLen);
    }
    state = State::SCANNING;
  } else {
    if (!tokenOverflow && cb.onString) {
      tokenBuf[tokenLen] = '\0';
      cb.onString(cb.ctx, tokenBuf, tokenLen);
    }
    state = State::SCANNING;
    expectingValue = false;
  }
}
