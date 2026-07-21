#pragma once

#include <ArduinoJson.h>

#include <string>

#include <PersistableStore.h>

class KarakeepConfigStore final : public PersistableStore<KarakeepConfigStore> {
  friend class PersistableStore<KarakeepConfigStore>;

 public:
  static constexpr size_t MAX_SERVER_URL_LENGTH = 255;
  static constexpr size_t MAX_TOKEN_LENGTH = 191;

  const std::string& getServerUrl() const { return serverUrl; }
  const std::string& getApiToken() const { return apiToken; }
  bool isConfigured() const { return !serverUrl.empty() && !apiToken.empty(); }
  void set(std::string serverUrl, std::string apiToken);
  void release();

  static const char* getFilePath() { return "/.crosspoint/karakeep.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

 private:
  KarakeepConfigStore() = default;

  std::string serverUrl;
  std::string apiToken;
};

#define KARAKEEP_STORE KarakeepConfigStore::getInstance()
