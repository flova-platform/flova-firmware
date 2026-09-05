#pragma once

#include <time.h>

#include <WiFiClientSecureBearSSL.h>
#include <umm_malloc/umm_heap_select.h>

#include <FlovaTlsRoots.h>

#ifndef FLOVA_HTTPS_TIMEOUT_MS
#define FLOVA_HTTPS_TIMEOUT_MS 15000
#endif
#ifndef FLOVA_ESP8266_OTA_TLS_RX_BYTES
#define FLOVA_ESP8266_OTA_TLS_RX_BYTES 16384
#endif
#ifndef FLOVA_ESP8266_LINK_TLS_RX_BYTES
#define FLOVA_ESP8266_LINK_TLS_RX_BYTES 2048
#endif
#ifndef FLOVA_ESP8266_TLS_TX_BYTES
#define FLOVA_ESP8266_TLS_TX_BYTES 512
#endif

namespace flova {

static const unsigned long kHttpsTimeoutMs = FLOVA_HTTPS_TIMEOUT_MS;
enum class TlsUse : uint8_t { Link, Ota };
enum class TlsResourceStatus : uint8_t { Ready, MemoryProfileMissing, InsufficientMemory };

struct TlsHeapStats {
  uint32_t dramFree = 0;
  uint32_t dramMaxBlock = 0;
  uint8_t dramFragmentation = 0;
  uint32_t iramFree = 0;
  uint32_t iramMaxBlock = 0;
  uint8_t iramFragmentation = 0;
  bool iramEnabled = false;
};

static const uint32_t kBearSslInputOverheadBytes = 325;
static const uint32_t kBearSslOutputOverheadBytes = 85;
static const uint32_t kTlsIramReserveBytes = 512;
static const uint32_t kTlsDramReserveBytes = 3072;
static const uint32_t kTlsDramBlockReserveBytes = 256;

inline TlsHeapStats tlsHeapStats() {
  TlsHeapStats stats;
  {
    HeapSelectDram dram;
    ESP.getHeapStats(&stats.dramFree, &stats.dramMaxBlock,
                     &stats.dramFragmentation);
  }
#if defined(MMU_IRAM_HEAP)
  {
    HeapSelectIram iram;
    ESP.getHeapStats(&stats.iramFree, &stats.iramMaxBlock,
                     &stats.iramFragmentation);
  }
  stats.iramEnabled = true;
#endif
  return stats;
}

inline uint32_t tlsReceiveBytes(TlsUse use) {
  return use == TlsUse::Link ? FLOVA_ESP8266_LINK_TLS_RX_BYTES
                             : FLOVA_ESP8266_OTA_TLS_RX_BYTES;
}

inline TlsResourceStatus tlsResourceStatus(TlsUse use,
                                           TlsHeapStats* observed = nullptr) {
  const TlsHeapStats stats = tlsHeapStats();
  if (observed) *observed = stats;
  if (!stats.iramEnabled) return TlsResourceStatus::MemoryProfileMissing;
  const uint32_t receiveAllocation = tlsReceiveBytes(use) + kBearSslInputOverheadBytes;
  const uint32_t transmitAllocation = FLOVA_ESP8266_TLS_TX_BYTES + kBearSslOutputOverheadBytes;
  const uint32_t iramRequired = receiveAllocation + transmitAllocation + kTlsIramReserveBytes;
  const uint32_t sslContextBytes = sizeof(br_ssl_client_context);
  const uint32_t x509ContextBytes = sizeof(br_x509_minimal_context);
  const uint32_t dramLargestAllocation =
      (sslContextBytes > x509ContextBytes ? sslContextBytes : x509ContextBytes) +
      kTlsDramBlockReserveBytes;
  const uint32_t dramRequired = sslContextBytes + x509ContextBytes + kTlsDramReserveBytes;
  return stats.iramFree >= iramRequired && stats.iramMaxBlock >= receiveAllocation &&
                 stats.dramFree >= dramRequired && stats.dramMaxBlock >= dramLargestAllocation
             ? TlsResourceStatus::Ready
             : TlsResourceStatus::InsufficientMemory;
}

inline void logTlsHeap(const char* stage, const TlsHeapStats& stats) {
  Serial.printf_P(PSTR("[flova] TLS heap %s dram_free=%u dram_max=%u dram_frag=%u%% iram_enabled=%u iram_free=%u iram_max=%u iram_frag=%u%%\n"),
                  stage, stats.dramFree, stats.dramMaxBlock, stats.dramFragmentation,
                  stats.iramEnabled ? 1 : 0, stats.iramFree, stats.iramMaxBlock,
                  stats.iramFragmentation);
}

inline const char* tlsResourceError(TlsResourceStatus status) {
  return status == TlsResourceStatus::MemoryProfileMissing
             ? "tls_memory_profile_missing"
             : "insufficient_tls_heap";
}

inline void configureOtaTls(BearSSL::WiFiClientSecure& client) {
  client.setBufferSizes(FLOVA_ESP8266_OTA_TLS_RX_BYTES, FLOVA_ESP8266_TLS_TX_BYTES);
}

inline void configureLinkTls(BearSSL::WiFiClientSecure& client,
                             BearSSL::X509List& trustAnchors, time_t now) {
  client.setTimeout(FLOVA_HTTPS_TIMEOUT_MS / 1000UL);
  client.setBufferSizes(FLOVA_ESP8266_LINK_TLS_RX_BYTES, FLOVA_ESP8266_TLS_TX_BYTES);
  client.setTrustAnchors(&trustAnchors);
  if (now >= 1700000000) client.setX509Time(now);
}

inline void logLinkTlsFailure(BearSSL::WiFiClientSecure& client) {
  char detail[96] = {};
  const int code = client.getLastSSLError(detail, sizeof(detail));
  Serial.printf("[flova] Link TLS connect failed code=%d detail=%.*s\n", code, 80, detail);
}

}  // namespace flova
