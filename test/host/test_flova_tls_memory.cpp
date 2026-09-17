#include <assert.h>
#include <FlovaEsp8266Memory.h>

int main() {
  using namespace flova;
  // Core 3.1.2 context sizes; the board passes sizeof() rather than hardcoding.
  const uint32_t contexts = 3408 + 3064;
  const uint32_t input = 16384 + kBearSslInputOverheadBytes;
  const uint32_t output = 512 + kBearSslOutputOverheadBytes;
  const uint32_t dram = contexts + kBearSslStackBytes + kTlsDramReserveBytes;
  TlsHeapStats heap;
  const auto status = [&](TlsUse use = TlsUse::Link, bool stack = false) {
    return tlsResourceStatusFor(heap, use, 16384, 512, 3408, 3064, stack);
  };
  heap.dramFree = dram + input + output;
  heap.dramMaxBlock = input;
  assert(status() == TlsResourceStatus::Ready);
  --heap.dramFree;
  assert(status() == TlsResourceStatus::InsufficientMemory);
  ++heap.dramFree;
  --heap.dramMaxBlock;
  assert(status() == TlsResourceStatus::InsufficientMemory);
  heap.dramMaxBlock = input;
  assert(status(TlsUse::Ota) == TlsResourceStatus::InsufficientMemory);
  heap.dramFree += 4096;
  assert(status(TlsUse::Ota) == TlsResourceStatus::Ready);
  // Optional IRAM may be exhausted. BearSSL is permitted to use DRAM instead.
  heap.iramEnabled = true;
  assert(status() == TlsResourceStatus::Ready);
  heap.dramFree = dram;
  heap.dramMaxBlock = kBearSslStackBytes + kTlsDramBlockReserveBytes;
  heap.iramFree = input + output + kTlsIramReserveBytes;
  heap.iramMaxBlock = input;
  assert(status() == TlsResourceStatus::Ready);
  --heap.iramMaxBlock;
  assert(status() == TlsResourceStatus::InsufficientMemory);
  heap.iramMaxBlock = input;
  heap.dramFree -= kBearSslStackBytes;
  assert(status() == TlsResourceStatus::InsufficientMemory);
  assert(status(TlsUse::Link, true) == TlsResourceStatus::Ready);

  const uint32_t boundedInput = 2048 + kBearSslInputOverheadBytes;
  const uint32_t boundedDram = contexts + kBearSslStackBytes + kTlsDramReserveBytes;
  heap = TlsHeapStats();
  heap.dramFree = boundedDram + boundedInput + output;
  heap.dramMaxBlock = kBearSslStackBytes + kTlsDramBlockReserveBytes;
  assert(tlsResourceStatusFor(heap, TlsUse::Link, 2048, 512, 3408, 3064,
                              false) == TlsResourceStatus::Ready);
  assert(heap.dramFree == 22810);
  --heap.dramFree;
  assert(tlsResourceStatusFor(heap, TlsUse::Link, 2048, 512, 3408, 3064,
                              false) == TlsResourceStatus::InsufficientMemory);
  heap.dramFree += 4097;
  assert(tlsResourceStatusFor(heap, TlsUse::Ota, 2048, 512, 3408, 3064,
                              false) == TlsResourceStatus::Ready);
  return 0;
}
