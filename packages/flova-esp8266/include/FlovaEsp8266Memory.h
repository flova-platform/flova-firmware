#pragma once

#include <stdint.h>

namespace flova {

enum class TlsUse : uint8_t { Link, Ota };
enum class TlsResourceStatus : uint8_t { Ready, InsufficientMemory };

struct TlsHeapStats {
  uint32_t dramFree = 0;
  uint32_t stackFree = 0;
  uint32_t tlsStackUsed = 0;
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
// Keep 4 KiB untouched for the application loop, plus 3 KiB for TCP and
// allocator/control-block overhead. OTA also needs the updater's flash page.
static const uint32_t kTlsDramReserveBytes = 4096 + 3072;
static const uint32_t kBearSslStackBytes = 6200;
static const uint32_t kTlsDramBlockReserveBytes = 256;

// Pure budget calculation, shared by the board preflight and host boundary
// tests. Trust anchors and existing Wi-Fi allocations are already in stats.
inline TlsResourceStatus tlsResourceStatusFor(
    const TlsHeapStats& stats, TlsUse use, uint32_t receiveBytes,
    uint32_t transmitBytes, uint32_t sslContextBytes,
    uint32_t x509ContextBytes, bool stackAllocated) {
  const uint32_t receiveAllocation = receiveBytes + kBearSslInputOverheadBytes;
  const uint32_t transmitAllocation = transmitBytes + kBearSslOutputOverheadBytes;
  const uint32_t iramRequired = receiveAllocation + transmitAllocation + kTlsIramReserveBytes;
  // WiFiClientSecure's constructor allocates the 6200-byte shared thunk
  // stack and aborts on OOM. Preflight must run before constructing it.
  const uint32_t stackBytes = stackAllocated ? 0 : kBearSslStackBytes;
  uint32_t dramLargestAllocation =
      (sslContextBytes > x509ContextBytes ? sslContextBytes : x509ContextBytes);
  if (stackBytes > dramLargestAllocation) dramLargestAllocation = stackBytes;
  dramLargestAllocation += kTlsDramBlockReserveBytes;
  const uint32_t dramRequired = sslContextBytes + x509ContextBytes + stackBytes +
      kTlsDramReserveBytes + (use == TlsUse::Ota ? 4096 : 0);
  const uint32_t largest = receiveAllocation > dramLargestAllocation
                               ? receiveAllocation : dramLargestAllocation;
  // Stock BearSSL falls back to DRAM for record buffers. An enabled but
  // exhausted optional IRAM heap must not reject a sufficient DRAM budget.
  const bool dramOnly = stats.dramFree >= dramRequired + receiveAllocation + transmitAllocation &&
                        stats.dramMaxBlock >= largest;
  const bool split = stats.iramEnabled && stats.iramFree >= iramRequired &&
      stats.iramMaxBlock >= receiveAllocation && stats.dramFree >= dramRequired &&
      stats.dramMaxBlock >= dramLargestAllocation;
  return dramOnly || split ? TlsResourceStatus::Ready : TlsResourceStatus::InsufficientMemory;
}

}  // namespace flova
