#pragma once
#include <HalStorage.h>
#include <Stream.h>

#include <functional>
#include <string>
#include <utility>

/**
 * HTTP client utility for fetching content and downloading files.
 * Wraps NetworkClientSecure and HTTPClient for HTTPS requests.
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  using CancelCallback = std::function<bool()>;

  struct Header {
    const char* name;
    const char* value;
  };

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
    SIZE_LIMIT_EXCEEDED,
  };

  struct DownloadOptions {
    explicit DownloadOptions(bool preservePartial = false, bool resumePartial = false,
                             CancelCallback shouldCancel = nullptr, size_t bufferSize = 1024, size_t maxBytes = 0)
        : preservePartial(preservePartial),
          resumePartial(resumePartial),
          shouldCancel(std::move(shouldCancel)),
          bufferSize(bufferSize),
          maxBytes(maxBytes) {}

    bool preservePartial;
    bool resumePartial;
    CancelCallback shouldCancel;
    size_t bufferSize;
    size_t maxBytes;
  };

  /**
   * Fetch text content from a URL with optional credentials.
   */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "", const Header* headers = nullptr, size_t headerCount = 0,
                       size_t maxBytes = 0);

  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "", const Header* headers = nullptr, size_t headerCount = 0,
                       size_t maxBytes = 0);

  /**
   * Download a file to the SD card with optional credentials.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "",
                                      DownloadOptions options = DownloadOptions(), const Header* headers = nullptr,
                                      size_t headerCount = 0);
};
