#include <cstdio>
#include <cstring>
#include <string>

#include "lib/Karakeep/KarakeepBookmarkParser.h"
#include "lib/Karakeep/KarakeepContentConverter.h"

namespace {
int failures = 0;

#define CHECK(condition)                                                                                      \
  do {                                                                                                        \
    if (!(condition)) {                                                                                       \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                             \
      ++failures;                                                                                             \
    }                                                                                                         \
  } while (0)

bool appendString(void* context, const uint8_t* data, const size_t len) {
  static_cast<std::string*>(context)->append(reinterpret_cast<const char*>(data), len);
  return true;
}

void testBookmarkListChunking() {
  const std::string oversizedHtml(700, 'x');
  const std::string json =
      "{\"bookmarks\":[{\"id\":\"b1\",\"title\":\"First\",\"content\":{\"type\":\"link\","
      "\"title\":\"Ignored\",\"author\":\"Ada\",\"htmlContent\":\"" +
      oversizedHtml +
      "\",\"contentAssetId\":\"a1\"}},{\"id\":\"b2\",\"title\":null,\"content\":{\"type\":\"text\","
      "\"title\":\"Note\"}}],\"nextCursor\":\"next-2\"}";

  KarakeepBookmarkEntry entries[2];
  KarakeepBookmarkParser parser(entries, 2);
  for (size_t offset = 0; offset < json.size(); offset += 7) {
    parser.feed(json.data() + offset, std::min<size_t>(7, json.size() - offset));
  }

  CHECK(!parser.hasError());
  CHECK(parser.getEntryCount() == 2);
  CHECK(std::strcmp(entries[0].id, "b1") == 0);
  CHECK(std::strcmp(entries[0].title, "First") == 0);
  CHECK(std::strcmp(entries[0].author, "Ada") == 0);
  CHECK(std::strcmp(entries[0].contentAssetId, "a1") == 0);
  CHECK(entries[0].type == KarakeepBookmarkType::LINK);
  CHECK(std::strcmp(entries[1].title, "Note") == 0);
  CHECK(entries[1].type == KarakeepBookmarkType::TEXT);
  CHECK(std::strcmp(parser.getNextCursor(), "next-2") == 0);
}

void testInlineHtmlConversion() {
  const char* json =
      R"({"content":{"htmlContent":"<article><h1>Hello &amp; goodbye</h1><script>bad()</script><p>One   two<br>three &#x1F642;</p></article>"}})";
  std::string output;
  KarakeepContentConverter converter(KarakeepContentConverter::Input::JSON_HTML,
                                     KarakeepOutput{&output, appendString});
  for (size_t offset = 0; offset < std::strlen(json); offset += 5) {
    CHECK(converter.feed(reinterpret_cast<const uint8_t*>(json + offset),
                         std::min<size_t>(5, std::strlen(json) - offset)));
  }
  CHECK(converter.finish());
  CHECK(converter.foundContent());
  if (output != "Hello & goodbye\n\nOne two\n\nthree 🙂\n") std::fprintf(stderr, "HTML output: [%s]\n", output.c_str());
  CHECK(output == "Hello & goodbye\n\nOne two\n\nthree 🙂\n");
}

void testTextBookmarkUnicode() {
  const char* json = R"({"content":{"type":"text","text":"Line one\nSnowman: \u2603"}})";
  std::string output;
  KarakeepContentConverter converter(KarakeepContentConverter::Input::JSON_TEXT,
                                     KarakeepOutput{&output, appendString});
  CHECK(converter.feed(reinterpret_cast<const uint8_t*>(json), std::strlen(json)));
  CHECK(converter.finish());
  if (output != "Line one\nSnowman: ☃") std::fprintf(stderr, "Text output: [%s]\n", output.c_str());
  CHECK(output == "Line one\nSnowman: ☃");
}
}  // namespace

int main() {
  testBookmarkListChunking();
  testInlineHtmlConversion();
  testTextBookmarkUnicode();
  std::printf("Karakeep tests: %s\n", failures == 0 ? "passed" : "failed");
  return failures == 0 ? 0 : 1;
}
