#pragma once

#include <KarakeepBookmarkParser.h>

#include <array>
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class KarakeepBrowserActivity final : public Activity {
 public:
  KarakeepBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string serverUrl,
                          std::string apiToken)
      : Activity("KarakeepBrowser", renderer, mappedInput),
        serverUrl(std::move(serverUrl)),
        apiToken(std::move(apiToken)),
        parser(nullptr, 0) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t { CHECK_WIFI, WIFI_SELECTION, LOADING, BROWSING, DOWNLOADING, ERROR };
  static constexpr size_t PAGE_LIMIT = 23;
  static constexpr size_t CURSOR_SIZE = 192;
  // Bounds pagination state to 6 KB while still allowing 759 bookmarks at 23 items per page.
  static constexpr size_t CURSOR_HISTORY_LIMIT = 32;

  ButtonNavigator buttonNavigator;
  State state = State::CHECK_WIFI;
  std::unique_ptr<KarakeepBookmarkEntry[]> entries;
  KarakeepBookmarkParser parser;
  size_t entryCount = 0;
  int selectorIndex = 0;
  std::string serverUrl;
  std::string apiToken;
  std::array<char, CURSOR_SIZE> cursor{};
  std::array<char, CURSOR_SIZE> nextCursor{};
  std::array<std::array<char, CURSOR_SIZE>, CURSOR_HISTORY_LIMIT> previousCursors{};
  size_t previousCursorCount = 0;
  std::string statusMessage;
  std::string errorMessage;

  bool preventAutoSleep() override;
  void connectOrFetch();
  void launchWifiSelection();
  void fetchPage();
  void openSelection();
  bool cacheBookmark(const KarakeepBookmarkEntry& entry, std::string& path);
  void openCachedArticle(const std::string& path);
  std::string apiBaseUrl() const;
  size_t visibleItemCount() const;
  bool hasPreviousPage() const;
  bool hasNextPage() const;
  int entryIndexForSelection() const;
};
