#pragma once

#include <cstddef>
#include <cstdint>

#include <StreamingJsonParser.h>

enum class KarakeepBookmarkType : uint8_t { UNKNOWN, LINK, TEXT, ASSET };

struct KarakeepBookmarkEntry {
  char id[64] = "";
  char title[192] = "";
  char author[96] = "";
  char contentAssetId[64] = "";
  KarakeepBookmarkType type = KarakeepBookmarkType::UNKNOWN;
};

class KarakeepBookmarkParser {
 public:
  KarakeepBookmarkParser(KarakeepBookmarkEntry* entries, size_t capacity);

  void reset(KarakeepBookmarkEntry* entries, size_t capacity);
  void feed(const char* data, size_t len);
  bool hasError() const;
  size_t getEntryCount() const;
  const char* getNextCursor() const;
  bool wasTruncated() const;

 private:
  enum class LastKey : uint8_t { NONE, BOOKMARKS, ID, TITLE, AUTHOR, TYPE, CONTENT, CONTENT_ASSET_ID, NEXT_CURSOR };

  static void onKey(void* ctx, const char* key, size_t len);
  static void onString(void* ctx, const char* value, size_t len);
  static void onNumber(void* ctx, const char* value, size_t len);
  static void onBool(void* ctx, bool value);
  static void onNull(void* ctx);
  static void onObjectStart(void* ctx);
  static void onObjectEnd(void* ctx);
  static void onArrayStart(void* ctx);
  static void onArrayEnd(void* ctx);

  void startContainer(bool object);
  void endContainer(bool object);
  void finishValue();

  StreamingJsonParser parser;
  KarakeepBookmarkEntry* entries;
  size_t capacity;
  size_t entryCount = 0;
  int currentEntry = -1;
  uint16_t depth = 0;
  uint16_t bookmarksDepth = 0;
  uint16_t contentDepth = 0;
  LastKey lastKey = LastKey::NONE;
  char nextCursor[192] = "";
  bool truncated = false;
};
