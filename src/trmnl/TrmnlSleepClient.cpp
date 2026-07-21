#include "TrmnlSleepClient.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>
#include <TrmnlDisplayJsonParser.h>
#include <WiFi.h>

#include <cstdio>

#include "WifiCredentialStore.h"
#include "network/HttpDownloader.h"
#include "util/UrlUtils.h"

namespace {
std::optional<WifiCredential> getTrmnlWifiCredential() {
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty()) {
    LOG_INF("TRM", "Trying last used WiFi: %s", lastSsid.c_str());
    if (auto cred = WIFI_STORE.findCredential(lastSsid)) {
      return cred;
    }
  }
  if (auto cred = WIFI_STORE.getCredentialAt(0)) {
    LOG_INF("TRM", "Trying first available WiFi: %s", cred->ssid.c_str());
    return cred;
  }
  LOG_ERR("TRM", "No WiFi credentials found");
  return std::nullopt;
}

bool validateBmpFile(const std::string& path) {
  FsFile file;
  if (!Storage.openFileForRead("TRM", path.c_str(), file)) {
    return false;
  }
  Bitmap bitmap(file, true);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok;
  file.close();
  return ok;
}
}  // namespace

bool TrmnlSleepClient::hasConfig(const Config& config) {
  return config.serverUrl && config.serverUrl[0] != '\0' && config.apiKey && config.apiKey[0] != '\0';
}

bool TrmnlSleepClient::connectWifi() {
  const auto cred = getTrmnlWifiCredential();
  if (!cred || cred->ssid.empty()) {
    return false;
  }

  LOG_INF("TRM", "Connecting to WiFi: %s", cred->ssid.c_str());
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.setSleep(false);
  WiFi.begin(cred->ssid.c_str(), cred->password.c_str());

  uint8_t attempt = 0;
  while (attempt < WIFI_RETRIES) {
    delay(WIFI_RETRY_DELAY_MS);
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      LOG_INF("TRM", "WiFi connected after %d attempts", attempt + 1);
      WIFI_STORE.setLastConnectedSsid(cred->ssid);
      return true;
    }
    attempt++;
  }

  LOG_ERR("TRM", "Wi-Fi connect failed after %d attempts", WIFI_RETRIES);
  return false;
}

void TrmnlSleepClient::disconnectWifi() {
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
}

std::string TrmnlSleepClient::resolveDeviceId(const char* configuredDeviceId) {
  if (configuredDeviceId && configuredDeviceId[0] != '\0') {
    return configuredDeviceId;
  }
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return macStr;
}

TrmnlSleepClient::ImageKind TrmnlSleepClient::detectImageKind(const std::string& path) {
  FsFile file;
  if (!Storage.openFileForRead("TRM", path.c_str(), file)) {
    return ImageKind::Unknown;
  }
  uint8_t header[8] = {};
  const int bytesRead = file.read(header, sizeof(header));
  file.close();
  if (bytesRead >= 2 && header[0] == 'B' && header[1] == 'M') {
    return ImageKind::Bmp;
  }
  if (bytesRead >= 8 && header[0] == 0x89 && header[1] == 'P' && header[2] == 'N' && header[3] == 'G' &&
      header[4] == '\r' && header[5] == '\n' && header[6] == 0x1A && header[7] == '\n') {
    return ImageKind::Png;
  }
  return ImageKind::Unknown;
}

bool TrmnlSleepClient::validateImage(const std::string& path, ImageKind& kind) {
  kind = detectImageKind(path);
  if (kind == ImageKind::Bmp) {
    return validateBmpFile(path);
  }
  return kind == ImageKind::Png;
}

const char* TrmnlSleepClient::cachePathFor(const ImageKind kind) {
  if (kind == ImageKind::Png) return CACHE_PNG;
  if (kind == ImageKind::Bmp) return CACHE_BMP;
  return nullptr;
}

bool TrmnlSleepClient::replaceCache(const std::string& tmpPath, const ImageKind kind) {
  const char* destPath = cachePathFor(kind);
  if (!destPath) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  const char* staleOther = kind == ImageKind::Png ? CACHE_BMP : CACHE_PNG;
  if (Storage.exists(destPath) && !Storage.remove(destPath)) {
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), destPath)) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (Storage.exists(staleOther)) {
    Storage.remove(staleOther);
  }
  return true;
}

bool TrmnlSleepClient::fetchLatest(const Config& config) {
  if (!hasConfig(config)) {
    const bool serverOk = config.serverUrl && config.serverUrl[0] != '\0';
    const bool apiKeyOk = config.apiKey && config.apiKey[0] != '\0';
    LOG_ERR("TRM", "TRMNL settings incomplete (serverUrl=%s apiKey=%s)",
            serverOk ? "ok" : "empty", apiKeyOk ? "ok" : "empty");
    return false;
  }

  LOG_INF("TRM", "Fetching latest TRMNL image from %s", config.serverUrl);
  Storage.mkdir("/.crosspoint");
  if (!connectWifi()) {
    LOG_ERR("TRM", "Failed to connect WiFi for TRMNL fetch");
    disconnectWifi();
    return false;
  }
  LOG_INF("TRM", "WiFi connected, fetching from server");

  const std::string deviceId = resolveDeviceId(config.deviceId);
  char widthHeader[8];
  char heightHeader[8];
  snprintf(widthHeader, sizeof(widthHeader), "%d", config.displaySize.width);
  snprintf(heightHeader, sizeof(heightHeader), "%d", config.displaySize.height);
  const HttpDownloader::Header headers[] = {{"ID", deviceId.c_str()},
                                            {"Access-Token", config.apiKey},
                                            {"Width", widthHeader},
                                            {"Height", heightHeader},
                                            {"Model", config.model ? config.model : "og_png"}};
  const std::string serverUrl = UrlUtils::ensureProtocol(config.serverUrl);
  const std::string displayUrl = UrlUtils::buildUrl(serverUrl, "/api/display");

  std::string response;
  bool fetched = HttpDownloader::fetchUrl(displayUrl, response, "", "", headers, sizeof(headers) / sizeof(headers[0]),
                                          DISPLAY_JSON_MAX_BYTES);
  if (fetched) {
    TrmnlDisplayJsonParser parser;
    parser.feed(response.c_str(), response.length());
    fetched = !parser.hasError() && parser.foundImageUrl();
    if (fetched) {
      const std::string imageUrl = UrlUtils::buildUrl(serverUrl, parser.getImageUrl());
      const bool sameHost = UrlUtils::extractHost(imageUrl) == UrlUtils::extractHost(serverUrl);
      const HttpDownloader::Header* imageHeaders = sameHost ? headers : nullptr;
      const size_t imageHeaderCount = sameHost ? sizeof(headers) / sizeof(headers[0]) : 0;
      if (Storage.exists(CACHE_TMP)) {
        Storage.remove(CACHE_TMP);
      }
      const HttpDownloader::DownloadOptions options(false, false, nullptr, 1024, IMAGE_MAX_BYTES);
      fetched = HttpDownloader::downloadToFile(imageUrl, CACHE_TMP, nullptr, nullptr, "", "", options, imageHeaders,
                                               imageHeaderCount) == HttpDownloader::OK;
      if (fetched) {
        ImageKind kind = ImageKind::Unknown;
        fetched = validateImage(CACHE_TMP, kind) && replaceCache(CACHE_TMP, kind);
      }
    }
  }

  disconnectWifi();
  if (!fetched) {
    LOG_ERR("TRM", "TRMNL fetch failed (fetch=%d validate=%d)", fetched ? 1 : 0, Storage.exists(CACHE_TMP) ? 1 : 0);
    Storage.remove(CACHE_TMP);
  } else {
    LOG_INF("TRM", "TRMNL image fetched successfully");
  }
  return fetched;
}
