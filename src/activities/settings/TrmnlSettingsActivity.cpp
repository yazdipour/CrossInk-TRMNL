#include "TrmnlSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "TrmnlSettingsStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace {
constexpr int MENU_ITEMS = 4;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_TRMNL_SERVER_URL, StrId::STR_TRMNL_API_KEY,
                                     StrId::STR_TRMNL_DEVICE_ID, StrId::STR_TRMNL_ORIENTATION};
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
    const std::string currentUrl = TRMNL_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TRMNL_SERVER_URL),
                                                                   prefillUrl, 159,
                                                                   InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               TRMNL_STORE.setServerUrl(urlToSave);
                               TRMNL_STORE.saveToFile();
                               requestUpdate();
                             }
                           });
  } else if (selectedIndex == 1) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TRMNL_API_KEY),
                                                                   TRMNL_STORE.getApiKey(),
                                                                   127,
                                                                   InputType::Password),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               TRMNL_STORE.setApiKey(kb.text);
                               TRMNL_STORE.saveToFile();
                               requestUpdate();
                             }
                           });
  } else if (selectedIndex == 2) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TRMNL_DEVICE_ID),
                                                                   TRMNL_STORE.getDeviceId(),
                                                                   31, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               TRMNL_STORE.setDeviceId(kb.text);
                               TRMNL_STORE.saveToFile();
                               requestUpdate();
                             }
                           });
  } else if (selectedIndex == 3) {
    TRMNL_STORE.setOrientation((TRMNL_STORE.getOrientation() + 1) % CrossPointSettings::TRMNL_ORIENTATION_COUNT);
    TRMNL_STORE.saveToFile();
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
        if (index == 0) return TRMNL_STORE.getServerUrl().empty() ? std::string(tr(STR_NOT_SET)) : TRMNL_STORE.getServerUrl();
        if (index == 1) return TRMNL_STORE.getApiKey().empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
        if (index == 2) return TRMNL_STORE.getDeviceId().empty() ? std::string(tr(STR_DEFAULT_VALUE)) : TRMNL_STORE.getDeviceId();
        if (index == 3) {
          return TRMNL_STORE.getOrientation() == CrossPointSettings::TRMNL_PORTRAIT ? std::string(tr(STR_TRMNL_VERTICAL))
                                                                                 : std::string(tr(STR_TRMNL_HORIZONTAL));
        }
        return std::string(tr(STR_NOT_SET));
      },
      true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
