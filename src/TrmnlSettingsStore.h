#pragma once
#include <string>

class TrmnlSettingsStore;
namespace JsonSettingsIO {
bool saveTrmnl(const TrmnlSettingsStore& store, const char* path);
bool loadTrmnl(TrmnlSettingsStore& store, const char* json, bool* needsResave);
}  // namespace JsonSettingsIO

class TrmnlSettingsStore {
 private:
  static TrmnlSettingsStore instance;

  std::string serverUrl;
  std::string apiKey;
  std::string deviceId;
  uint8_t orientation = 0; // TRMNL_LANDSCAPE = 0

  TrmnlSettingsStore() = default;

  friend bool JsonSettingsIO::saveTrmnl(const TrmnlSettingsStore&, const char*);
  friend bool JsonSettingsIO::loadTrmnl(TrmnlSettingsStore&, const char*, bool*);

 public:
  TrmnlSettingsStore(const TrmnlSettingsStore&) = delete;
  TrmnlSettingsStore& operator=(const TrmnlSettingsStore&) = delete;

  static TrmnlSettingsStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();

  const std::string& getServerUrl() const { return serverUrl; }
  void setServerUrl(const std::string& url) { serverUrl = url; }

  const std::string& getApiKey() const { return apiKey; }
  void setApiKey(const std::string& key) { apiKey = key; }

  const std::string& getDeviceId() const { return deviceId; }
  void setDeviceId(const std::string& id) { deviceId = id; }

  uint8_t getOrientation() const { return orientation; }
  void setOrientation(uint8_t orient) { orientation = orient; }
};

#define TRMNL_STORE TrmnlSettingsStore::getInstance()
