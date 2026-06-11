#include "TrmnlSettingsStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <cstring>

TrmnlSettingsStore TrmnlSettingsStore::instance;

namespace {
constexpr char TRMNL_FILE_JSON[] = "/.crosspoint/trmnl.json";
}  // namespace

bool TrmnlSettingsStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveTrmnl(*this, TRMNL_FILE_JSON);
}

bool TrmnlSettingsStore::loadFromFile() {
  if (Storage.exists(TRMNL_FILE_JSON)) {
    String json = Storage.readFile(TRMNL_FILE_JSON);
    if (!json.isEmpty()) {
      bool resave = false;
      bool result = JsonSettingsIO::loadTrmnl(*this, json.c_str(), &resave);
      if (result && resave) {
        LOG_DBG("TRM", "Resaving JSON with obfuscated API key");
        saveToFile();
      }
      return result;
    }
  }

  return false;
}
