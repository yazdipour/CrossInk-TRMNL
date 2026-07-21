#include "KarakeepBookmarkParser.h"

#include <cstring>

namespace {
void copyBounded(char* dst, const size_t dstSize, const char* src, const size_t srcLen) {
  const size_t count = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, count);
  dst[count] = '\0';
}

bool keyEquals(const char* key, const size_t len, const char* expected) {
  return strlen(expected) == len && memcmp(key, expected, len) == 0;
}
}  // namespace

KarakeepBookmarkParser::KarakeepBookmarkParser(KarakeepBookmarkEntry* entries, const size_t capacity)
    : parser(JsonCallbacks{this, onKey, onString, onNumber, onBool, onNull, onObjectStart, onObjectEnd, onArrayStart,
                           onArrayEnd}),
      entries(entries),
      capacity(capacity) {}

void KarakeepBookmarkParser::reset(KarakeepBookmarkEntry* newEntries, const size_t newCapacity) {
  parser.reset();
  entries = newEntries;
  capacity = newCapacity;
  entryCount = 0;
  currentEntry = -1;
  depth = 0;
  bookmarksDepth = 0;
  contentDepth = 0;
  lastKey = LastKey::NONE;
  nextCursor[0] = '\0';
  truncated = false;
}

void KarakeepBookmarkParser::feed(const char* data, const size_t len) { parser.feed(data, len); }
bool KarakeepBookmarkParser::hasError() const { return parser.hasError(); }
size_t KarakeepBookmarkParser::getEntryCount() const { return entryCount; }
const char* KarakeepBookmarkParser::getNextCursor() const { return nextCursor; }
bool KarakeepBookmarkParser::wasTruncated() const { return truncated; }

void KarakeepBookmarkParser::onKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<KarakeepBookmarkParser*>(ctx);
  if (keyEquals(key, len, "bookmarks"))
    self->lastKey = LastKey::BOOKMARKS;
  else if (keyEquals(key, len, "id"))
    self->lastKey = LastKey::ID;
  else if (keyEquals(key, len, "title"))
    self->lastKey = LastKey::TITLE;
  else if (keyEquals(key, len, "author"))
    self->lastKey = LastKey::AUTHOR;
  else if (keyEquals(key, len, "type"))
    self->lastKey = LastKey::TYPE;
  else if (keyEquals(key, len, "content"))
    self->lastKey = LastKey::CONTENT;
  else if (keyEquals(key, len, "contentAssetId"))
    self->lastKey = LastKey::CONTENT_ASSET_ID;
  else if (keyEquals(key, len, "nextCursor"))
    self->lastKey = LastKey::NEXT_CURSOR;
  else
    self->lastKey = LastKey::NONE;
}

void KarakeepBookmarkParser::onString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<KarakeepBookmarkParser*>(ctx);
  if (self->depth == 1 && self->lastKey == LastKey::NEXT_CURSOR) {
    copyBounded(self->nextCursor, sizeof(self->nextCursor), value, len);
    self->finishValue();
    return;
  }
  if (self->currentEntry < 0 || static_cast<size_t>(self->currentEntry) >= self->capacity) {
    self->finishValue();
    return;
  }

  auto& entry = self->entries[self->currentEntry];
  const bool atBookmark = self->depth == self->bookmarksDepth + 1;
  const bool inContent = self->contentDepth > 0 && self->depth == self->contentDepth;
  if (atBookmark && self->lastKey == LastKey::ID) {
    copyBounded(entry.id, sizeof(entry.id), value, len);
  } else if (atBookmark && self->lastKey == LastKey::TITLE) {
    copyBounded(entry.title, sizeof(entry.title), value, len);
  } else if (inContent && self->lastKey == LastKey::TITLE && entry.title[0] == '\0') {
    copyBounded(entry.title, sizeof(entry.title), value, len);
  } else if (inContent && self->lastKey == LastKey::AUTHOR) {
    copyBounded(entry.author, sizeof(entry.author), value, len);
  } else if (inContent && self->lastKey == LastKey::CONTENT_ASSET_ID) {
    copyBounded(entry.contentAssetId, sizeof(entry.contentAssetId), value, len);
  } else if (inContent && self->lastKey == LastKey::TYPE) {
    if (len == 4 && memcmp(value, "link", 4) == 0)
      entry.type = KarakeepBookmarkType::LINK;
    else if (len == 4 && memcmp(value, "text", 4) == 0)
      entry.type = KarakeepBookmarkType::TEXT;
    else if (len == 5 && memcmp(value, "asset", 5) == 0)
      entry.type = KarakeepBookmarkType::ASSET;
  }
  self->finishValue();
}

void KarakeepBookmarkParser::onNumber(void* ctx, const char*, const size_t) {
  static_cast<KarakeepBookmarkParser*>(ctx)->finishValue();
}
void KarakeepBookmarkParser::onBool(void* ctx, const bool) {
  static_cast<KarakeepBookmarkParser*>(ctx)->finishValue();
}
void KarakeepBookmarkParser::onNull(void* ctx) { static_cast<KarakeepBookmarkParser*>(ctx)->finishValue(); }
void KarakeepBookmarkParser::onObjectStart(void* ctx) {
  static_cast<KarakeepBookmarkParser*>(ctx)->startContainer(true);
}
void KarakeepBookmarkParser::onObjectEnd(void* ctx) { static_cast<KarakeepBookmarkParser*>(ctx)->endContainer(true); }
void KarakeepBookmarkParser::onArrayStart(void* ctx) {
  static_cast<KarakeepBookmarkParser*>(ctx)->startContainer(false);
}
void KarakeepBookmarkParser::onArrayEnd(void* ctx) { static_cast<KarakeepBookmarkParser*>(ctx)->endContainer(false); }

void KarakeepBookmarkParser::startContainer(const bool object) {
  ++depth;
  if (!object && lastKey == LastKey::BOOKMARKS) {
    bookmarksDepth = depth;
  } else if (object && bookmarksDepth > 0 && depth == bookmarksDepth + 1) {
    if (entryCount < capacity) {
      entries[entryCount] = KarakeepBookmarkEntry{};
      currentEntry = static_cast<int>(entryCount++);
    } else {
      currentEntry = -1;
      truncated = true;
    }
  } else if (object && currentEntry >= 0 && lastKey == LastKey::CONTENT && depth == bookmarksDepth + 2) {
    contentDepth = depth;
  }
  lastKey = LastKey::NONE;
}

void KarakeepBookmarkParser::endContainer(const bool object) {
  if (object && depth == contentDepth) contentDepth = 0;
  if (object && bookmarksDepth > 0 && depth == bookmarksDepth + 1) currentEntry = -1;
  if (!object && depth == bookmarksDepth) bookmarksDepth = 0;
  if (depth > 0) --depth;
  lastKey = LastKey::NONE;
}

void KarakeepBookmarkParser::finishValue() { lastKey = LastKey::NONE; }
