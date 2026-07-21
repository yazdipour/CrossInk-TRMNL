#include "KarakeepConfigStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <utility>

void KarakeepConfigStore::toJson(JsonDocument& doc) const {
  doc["serverUrl"] = serverUrl;
  doc["apiToken_obf"] = obfuscation::obfuscateToBase64(apiToken);
}

bool KarakeepConfigStore::fromJson(const JsonVariantConst doc) {
  serverUrl = doc["serverUrl"] | "";
  obfuscation::DecodeStatus status = obfuscation::DecodeStatus::INVALID;
  apiToken = obfuscation::deobfuscateFromBase64(doc["apiToken_obf"] | "", &status);
  bool needsResave = status == obfuscation::DecodeStatus::LEGACY;
  if (apiToken.empty()) {
    apiToken = doc["apiToken"] | "";
    needsResave = !apiToken.empty();
  }
  if (status == obfuscation::DecodeStatus::INVALID && apiToken.empty()) {
    LOG_ERR("KKS", "Ignoring unreadable Karakeep API token");
  }
  if (needsResave) saveToFile();
  return true;
}

void KarakeepConfigStore::set(std::string newServerUrl, std::string newApiToken) {
  while (!newServerUrl.empty() && newServerUrl.back() == '/') newServerUrl.pop_back();
  serverUrl = std::move(newServerUrl);
  apiToken = std::move(newApiToken);
}

void KarakeepConfigStore::release() {
  std::string().swap(serverUrl);
  std::string().swap(apiToken);
}
