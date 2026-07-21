#include "KarakeepBrowserActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <KarakeepContentConverter.h>
#include <Logging.h>
#include <Memory.h>
#include <Stream.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>
#include <utility>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/StringUtils.h"

namespace {
constexpr size_t LIST_MAX_BYTES = 512U * 1024U;
constexpr size_t CONTENT_MAX_BYTES = 4U * 1024U * 1024U;
constexpr char CACHE_DIR[] = "/karakeep";

class ParserStream final : public Stream {
 public:
  explicit ParserStream(KarakeepBookmarkParser& parser) : parser(parser) {}
  int available() override { return 0; }
  int peek() override { return -1; }
  int read() override { return -1; }
  size_t write(const uint8_t c) override {
    parser.feed(reinterpret_cast<const char*>(&c), 1);
    return 1;
  }
  size_t write(const uint8_t* data, const size_t len) override {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return parser.hasError() ? 0 : len;
  }

 private:
  KarakeepBookmarkParser& parser;
};

class ConverterStream final : public Stream {
 public:
  explicit ConverterStream(KarakeepContentConverter& converter) : converter(converter) {}
  int available() override { return 0; }
  int peek() override { return -1; }
  int read() override { return -1; }
  size_t write(const uint8_t c) override { return converter.feed(&c, 1) ? 1 : 0; }
  size_t write(const uint8_t* data, const size_t len) override { return converter.feed(data, len) ? len : 0; }

 private:
  KarakeepContentConverter& converter;
};

std::string percentEncode(const char* value) {
  std::string encoded;
  encoded.reserve(strlen(value) * 3);
  for (const unsigned char c : std::string_view(value)) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else {
      char escaped[4];
      snprintf(escaped, sizeof(escaped), "%%%02X", c);
      encoded += escaped;
    }
  }
  return encoded;
}

bool writeToFile(void* context, const uint8_t* data, const size_t len) {
  return static_cast<HalFile*>(context)->write(data, len) == len;
}
}  // namespace

void KarakeepBrowserActivity::onEnter() {
  Activity::onEnter();
  state = State::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  // One bounded activity-lifetime allocation (~10 KB); stack/static storage would outlive or overflow the task stack.
  entries = makeUniqueNoThrow<KarakeepBookmarkEntry[]>(PAGE_LIMIT);
  if (!entries) {
    LOG_ERR("KKB", "OOM: %zu Karakeep entries (free=%u maxAlloc=%u)", PAGE_LIMIT, ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    state = State::ERROR;
    errorMessage = tr(STR_MEMORY_ERROR);
    requestUpdate();
    return;
  }
  parser.reset(entries.get(), PAGE_LIMIT);
  requestUpdate();
  connectOrFetch();
}

void KarakeepBrowserActivity::onExit() {
  entries.reset();
  std::string().swap(apiToken);
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
  }
  Activity::onExit();
  silentRestart();
}

void KarakeepBrowserActivity::loop() {
  if (state == State::WIFI_SELECTION || state == State::LOADING || state == State::DOWNLOADING ||
      state == State::CHECK_WIFI) {
    return;
  }
  if (state == State::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) connectOrFetch();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) onGoHome();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelection();
    return;
  }

  const int count = static_cast<int>(visibleItemCount());
  buttonNavigator.onNextRelease([this, count] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, count] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, count] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, count, 20);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, count] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, count, 20);
    requestUpdate();
  });
}

void KarakeepBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_KARAKEEP), true, EpdFontFamily::BOLD);

  if (state == State::CHECK_WIFI || state == State::LOADING || state == State::DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, statusMessage.c_str());
  } else if (state == State::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2 + 10, errorMessage.c_str());
  } else if (visibleItemCount() == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, tr(STR_NO_ENTRIES));
  } else {
    constexpr int rowHeight = 30;
    constexpr int top = 60;
    constexpr int rowsPerScreen = 13;
    const int pageStart = selectorIndex / rowsPerScreen * rowsPerScreen;
    const int count = static_cast<int>(visibleItemCount());
    renderer.fillRect(0, top + (selectorIndex % rowsPerScreen) * rowHeight - 2, width - 1, rowHeight);
    for (int item = pageStart; item < count && item < pageStart + rowsPerScreen; ++item) {
      char label[256];
      if (hasPreviousPage() && item == 0) {
        snprintf(label, sizeof(label), "< %s", tr(STR_PREV_PAGE));
      } else if (hasNextPage() && item == count - 1) {
        snprintf(label, sizeof(label), "> %s", tr(STR_NEXT_PAGE));
      } else {
        const int entryIndex = item - (hasPreviousPage() ? 1 : 0);
        const auto& entry = entries[entryIndex];
        if (entry.author[0]) {
          snprintf(label, sizeof(label), "%s — %s", entry.title[0] ? entry.title : tr(STR_UNTITLED), entry.author);
        } else {
          snprintf(label, sizeof(label), "%s", entry.title[0] ? entry.title : tr(STR_UNTITLED));
        }
      }
      const auto truncated = renderer.truncatedText(UI_10_FONT_ID, label, width - 40);
      renderer.drawText(UI_10_FONT_ID, 20, top + (item % rowsPerScreen) * rowHeight, truncated.c_str(),
                        item != selectorIndex);
    }
  }

  const char* confirm = state == State::ERROR ? tr(STR_RETRY) : tr(STR_OPEN);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

bool KarakeepBrowserActivity::preventAutoSleep() {
  return state == State::CHECK_WIFI || state == State::WIFI_SELECTION || state == State::LOADING ||
         state == State::DOWNLOADING;
}

void KarakeepBrowserActivity::connectOrFetch() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    fetchPage();
  } else {
    launchWifiSelection();
  }
}

void KarakeepBrowserActivity::launchWifiSelection() {
  state = State::WIFI_SELECTION;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("KKB", "OOM: Wi-Fi selection activity");
    state = State::ERROR;
    errorMessage = tr(STR_MEMORY_ERROR);
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      state = State::ERROR;
      errorMessage = tr(STR_WIFI_CONN_FAILED);
      requestUpdate();
    } else {
      fetchPage();
    }
  });
}

void KarakeepBrowserActivity::fetchPage() {
  state = State::LOADING;
  statusMessage = tr(STR_LOADING);
  requestUpdateAndWait();
  parser.reset(entries.get(), PAGE_LIMIT);

  std::string url = apiBaseUrl() + "/bookmarks?limit=" + std::to_string(PAGE_LIMIT) + "&includeContent=false";
  if (cursor[0]) url += "&cursor=" + percentEncode(cursor.data());
  std::string authorization = "Bearer " + apiToken;
  const HttpDownloader::Header headers[] = {{"Authorization", authorization.c_str()}, {"Accept", "application/json"}};
  ParserStream stream(parser);
  if (!HttpDownloader::fetchUrl(url, stream, "", "", headers, std::size(headers), LIST_MAX_BYTES) ||
      parser.hasError()) {
    LOG_ERR("KKB", "Failed to fetch/parse bookmark page");
    state = State::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }

  entryCount = parser.getEntryCount();
  snprintf(nextCursor.data(), nextCursor.size(), "%s", parser.getNextCursor());
  selectorIndex = 0;
  state = State::BROWSING;
  requestUpdate();
}

void KarakeepBrowserActivity::openSelection() {
  if (hasPreviousPage() && selectorIndex == 0) {
    --previousCursorCount;
    snprintf(cursor.data(), cursor.size(), "%s", previousCursors[previousCursorCount].data());
    fetchPage();
    return;
  }
  if (hasNextPage() && selectorIndex == static_cast<int>(visibleItemCount()) - 1) {
    snprintf(previousCursors[previousCursorCount].data(), previousCursors[previousCursorCount].size(), "%s",
             cursor.data());
    ++previousCursorCount;
    snprintf(cursor.data(), cursor.size(), "%s", nextCursor.data());
    fetchPage();
    return;
  }
  const int entryIndex = entryIndexForSelection();
  if (entryIndex < 0 || entryIndex >= static_cast<int>(entryCount)) return;

  const auto& entry = entries[entryIndex];
  if (entry.type == KarakeepBookmarkType::ASSET || entry.type == KarakeepBookmarkType::UNKNOWN) {
    state = State::ERROR;
    errorMessage = tr(STR_KARAKEEP_UNSUPPORTED);
    requestUpdate();
    return;
  }
  state = State::DOWNLOADING;
  statusMessage = tr(STR_DOWNLOADING);
  requestUpdateAndWait();
  std::string path;
  if (!cacheBookmark(entry, path)) {
    state = State::ERROR;
    errorMessage = tr(STR_DOWNLOAD_FAILED);
    requestUpdate();
    return;
  }
  openCachedArticle(path);
}

bool KarakeepBrowserActivity::cacheBookmark(const KarakeepBookmarkEntry& entry, std::string& path) {
  if (!Storage.exists(CACHE_DIR) && !Storage.mkdir(CACHE_DIR)) {
    LOG_ERR("KKB", "Failed to create %s", CACHE_DIR);
    return false;
  }
  const std::string safeTitle = StringUtils::sanitizeFilename(entry.title[0] ? entry.title : "Karakeep", 96);
  const std::string safeId = StringUtils::sanitizeFilename(entry.id, 48);
  if (safeId.empty()) {
    LOG_ERR("KKB", "Rejected bookmark with invalid ID");
    return false;
  }
  path = std::string(CACHE_DIR) + "/" + safeTitle + "_" + safeId + ".txt";
  if (Storage.exists(path.c_str())) return true;

  const std::string tempPath = path + ".tmp";
  if (Storage.exists(tempPath.c_str())) Storage.remove(tempPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("KKB", tempPath, file)) {
    LOG_ERR("KKB", "Failed to open article cache: %s", tempPath.c_str());
    return false;
  }
  const char* title = entry.title[0] ? entry.title : tr(STR_UNTITLED);
  bool headerWritten = file.write(reinterpret_cast<const uint8_t*>(title), strlen(title)) == strlen(title) &&
                       file.write(reinterpret_cast<const uint8_t*>("\n"), 1) == 1;
  if (entry.author[0]) {
    headerWritten = headerWritten &&
                    file.write(reinterpret_cast<const uint8_t*>(entry.author), strlen(entry.author)) ==
                        strlen(entry.author) &&
                    file.write(reinterpret_cast<const uint8_t*>("\n"), 1) == 1;
  }
  headerWritten = headerWritten && file.write(reinterpret_cast<const uint8_t*>("\n"), 1) == 1;
  if (!headerWritten) {
    LOG_ERR("KKB", "Failed to write article cache header: %s", tempPath.c_str());
    file.close();
    Storage.remove(tempPath.c_str());
    return false;
  }

  const bool assetBacked = entry.contentAssetId[0] != '\0';
  const auto input = assetBacked ? KarakeepContentConverter::Input::HTML
                                 : (entry.type == KarakeepBookmarkType::TEXT
                                        ? KarakeepContentConverter::Input::JSON_TEXT
                                        : KarakeepContentConverter::Input::JSON_HTML);
  KarakeepContentConverter converter(input, KarakeepOutput{&file, writeToFile});
  ConverterStream stream(converter);
  std::string url = assetBacked ? apiBaseUrl() + "/assets/" + percentEncode(entry.contentAssetId)
                                : apiBaseUrl() + "/bookmarks/" + percentEncode(entry.id) + "?includeContent=true";
  std::string authorization = "Bearer " + apiToken;
  const HttpDownloader::Header headers[] = {{"Authorization", authorization.c_str()}};
  const bool fetched = HttpDownloader::fetchUrl(url, stream, "", "", headers, std::size(headers), CONTENT_MAX_BYTES);
  const bool converted = fetched && converter.finish();
  file.flush();
  file.close();
  if (!converted) {
    LOG_ERR("KKB", "Failed to cache content for bookmark %s", entry.id);
    Storage.remove(tempPath.c_str());
    return false;
  }
  if (!Storage.rename(tempPath.c_str(), path.c_str())) {
    LOG_ERR("KKB", "Failed to promote article cache: %s", path.c_str());
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}

void KarakeepBrowserActivity::openCachedArticle(const std::string& path) {
  APP_STATE.openEpubPath = path;
  APP_STATE.saveToFile();
  silentRestartToReader();
}

std::string KarakeepBrowserActivity::apiBaseUrl() const {
  if (serverUrl.ends_with("/api/v1")) return serverUrl;
  return serverUrl + "/api/v1";
}

size_t KarakeepBrowserActivity::visibleItemCount() const {
  return entryCount + (hasPreviousPage() ? 1 : 0) + (hasNextPage() ? 1 : 0);
}
bool KarakeepBrowserActivity::hasPreviousPage() const { return previousCursorCount != 0; }
bool KarakeepBrowserActivity::hasNextPage() const {
  return nextCursor[0] != '\0' && previousCursorCount < CURSOR_HISTORY_LIMIT;
}
int KarakeepBrowserActivity::entryIndexForSelection() const { return selectorIndex - (hasPreviousPage() ? 1 : 0); }
