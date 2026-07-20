#include "TrmnlSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace {
constexpr int MENU_ITEMS = 5;
// Named so this item's wiring in handleSelection()/render() doesn't silently break if
// items are reordered or new ones inserted before it -- the other indices (0-2) are
// pre-existing and unchanged by this feature, left as-is to keep this change scoped.
constexpr int MENU_INDEX_EXTENDED_WIFI_TIMEOUT = 4;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_TRMNL_SERVER_URL, StrId::STR_TRMNL_API_KEY,
                                     StrId::STR_TRMNL_DEVICE_ID, StrId::STR_TRMNL_ORIENTATION,
                                     StrId::STR_TRMNL_EXTENDED_WIFI_TIMEOUT};

void copySetting(char* dest, const size_t destSize, const std::string& value) {
  strncpy(dest, value.c_str(), destSize - 1);
  dest[destSize - 1] = '\0';
}
}  // namespace

void TrmnlSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void TrmnlSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }
  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    requestUpdate();
  });
}

void TrmnlSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    const std::string currentUrl = SETTINGS.trmnlServerUrl;
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TRMNL_SERVER_URL),
                                                                   prefillUrl, sizeof(SETTINGS.trmnlServerUrl) - 1,
                                                                   InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               copySetting(SETTINGS.trmnlServerUrl, sizeof(SETTINGS.trmnlServerUrl), urlToSave);
                               SETTINGS.saveToFile();
                               requestUpdate();
                             }
                           });
  } else if (selectedIndex == 1) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TRMNL_API_KEY),
                                                                   SETTINGS.trmnlApiKey,
                                                                   sizeof(SETTINGS.trmnlApiKey) - 1,
                                                                   InputType::Password),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               copySetting(SETTINGS.trmnlApiKey, sizeof(SETTINGS.trmnlApiKey), kb.text);
                               SETTINGS.saveToFile();
                               requestUpdate();
                             }
                           });
  } else if (selectedIndex == 2) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TRMNL_DEVICE_ID),
                                                                   SETTINGS.trmnlDeviceId,
                                                                   sizeof(SETTINGS.trmnlDeviceId) - 1, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               copySetting(SETTINGS.trmnlDeviceId, sizeof(SETTINGS.trmnlDeviceId), kb.text);
                               SETTINGS.saveToFile();
                               requestUpdate();
                             }
                           });
  } else if (selectedIndex == 3) {
    SETTINGS.trmnlOrientation = (SETTINGS.trmnlOrientation + 1) % CrossPointSettings::TRMNL_ORIENTATION_COUNT;
    SETTINGS.saveToFile();
    requestUpdate();
  } else if (selectedIndex == MENU_INDEX_EXTENDED_WIFI_TIMEOUT) {
    SETTINGS.trmnlExtendedWifiTimeout = !SETTINGS.trmnlExtendedWifiTimeout;
    SETTINGS.saveToFile();
    requestUpdate();
  }
}

void TrmnlSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TRMNL_SETTINGS));
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, static_cast<int>(selectedIndex),
      [](int index) { return std::string(I18N.get(menuNames[index])); }, nullptr, nullptr,
      [](int index) {
        if (index == 0) return SETTINGS.trmnlServerUrl[0] == '\0' ? std::string(tr(STR_NOT_SET)) : SETTINGS.trmnlServerUrl;
        if (index == 1) return SETTINGS.trmnlApiKey[0] == '\0' ? std::string(tr(STR_NOT_SET)) : std::string("******");
        if (index == 2) return SETTINGS.trmnlDeviceId[0] == '\0' ? std::string(tr(STR_DEFAULT_VALUE)) : SETTINGS.trmnlDeviceId;
        if (index == 3) {
          return SETTINGS.trmnlOrientation == CrossPointSettings::TRMNL_PORTRAIT ? std::string(tr(STR_TRMNL_VERTICAL))
                                                                                 : std::string(tr(STR_TRMNL_HORIZONTAL));
        }
        if (index == MENU_INDEX_EXTENDED_WIFI_TIMEOUT) {
          return SETTINGS.trmnlExtendedWifiTimeout ? std::string(tr(STR_STATE_ON)) : std::string(tr(STR_STATE_OFF));
        }
        return std::string(tr(STR_NOT_SET));
      },
      true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
