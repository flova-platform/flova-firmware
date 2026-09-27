#pragma once

#include <FlovaFlashLog.h>

#include <time.h>

#include <WiFiClientSecureBearSSL.h>
#include <umm_malloc/umm_heap_select.h>
#include <StackThunk.h>

#include <FlovaTlsRoots.h>
#include <FlovaEsp8266Memory.h>

#ifndef FLOVA_HTTPS_TIMEOUT_MS
#define FLOVA_HTTPS_TIMEOUT_MS 15000
#endif
#ifndef FLOVA_ESP8266_OTA_TLS_RX_BYTES
#if defined(FLOVA_ESP8266_BOUNDED_TLS_RECORDS)
#define FLOVA_ESP8266_OTA_TLS_RX_BYTES 2048
#else
#define FLOVA_ESP8266_OTA_TLS_RX_BYTES 16384
#endif
#endif
#ifndef FLOVA_ESP8266_LINK_TLS_RX_BYTES
#if defined(FLOVA_ESP8266_BOUNDED_TLS_RECORDS)
#define FLOVA_ESP8266_LINK_TLS_RX_BYTES 2048
#else
#define FLOVA_ESP8266_LINK_TLS_RX_BYTES 16384
#endif
#endif
#ifndef FLOVA_ESP8266_TLS_TX_BYTES
#define FLOVA_ESP8266_TLS_TX_BYTES 512
#endif

namespace flova {

#if !defined(FLOVA_ESP8266_BOUNDED_TLS_RECORDS)
static_assert(FLOVA_ESP8266_LINK_TLS_RX_BYTES == 16384,
              "Small Link TLS buffers require a record-bounded endpoint");
static_assert(FLOVA_ESP8266_OTA_TLS_RX_BYTES == 16384,
              "Small OTA TLS buffers require a record-bounded endpoint");
#endif

static const unsigned long kHttpsTimeoutMs = FLOVA_HTTPS_TIMEOUT_MS;
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
  stats.stackFree = ESP.getFreeContStack();
  stats.tlsStackUsed = stack_thunk_get_refcnt() ? stack_thunk_get_max_usage() : 0;
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
  return tlsResourceStatusFor(stats, use, tlsReceiveBytes(use),
      FLOVA_ESP8266_TLS_TX_BYTES, sizeof(br_ssl_client_context),
      sizeof(br_x509_minimal_context), stack_thunk_get_refcnt() != 0);
}

inline void logTlsHeap(const char* stage, const TlsHeapStats& stats) {
#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_DEBUG
  FLOVA_SERIAL_PRINTF_DEBUG("[flova] TLS heap %s dram_free=%u dram_max=%u dram_frag=%u%% iram_enabled=%u iram_free=%u iram_max=%u iram_frag=%u%% stack_free=%u tls_stack_used=%u\n",
                  stage, stats.dramFree, stats.dramMaxBlock, stats.dramFragmentation,
                  stats.iramEnabled ? 1 : 0, stats.iramFree, stats.iramMaxBlock,
                  stats.iramFragmentation, stats.stackFree, stats.tlsStackUsed);
#else
  (void)stage;
  (void)stats;
#endif
}

inline const char* tlsResourceError(TlsResourceStatus) {
  return "insufficient_tls_heap";
}

inline void configureOtaTls(BearSSL::WiFiClientSecure& client) {
  client.setBufferSizes(FLOVA_ESP8266_OTA_TLS_RX_BYTES, FLOVA_ESP8266_TLS_TX_BYTES);
}

inline void configureLinkTls(BearSSL::WiFiClientSecure& client,
                             BearSSL::X509List& trustAnchors, time_t now) {
  client.setTimeout(FLOVA_HTTPS_TIMEOUT_MS);
  client.setBufferSizes(FLOVA_ESP8266_LINK_TLS_RX_BYTES, FLOVA_ESP8266_TLS_TX_BYTES);
  client.setTrustAnchors(&trustAnchors);
  if (now >= 1700000000) client.setX509Time(now);
}

inline void logLinkTlsFailure(BearSSL::WiFiClientSecure& client) {
#if FLOVA_LOGGING_ENABLED && FLOVA_LOG_LEVEL >= FLOVA_LOG_LEVEL_ERROR
  char detail[96] = {};
  const int code = client.getLastSSLError(detail, sizeof(detail));
  FLOVA_SERIAL_PRINTF_ERROR("[flova] Link TLS connect failed code=%d detail=%.*s\n", code, 80, detail);
#else
  (void)client;
#endif
}

}  // namespace flova
