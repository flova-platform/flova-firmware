Import("env")

from hashlib import sha256
from json import loads
from pathlib import Path


if env.get("PIOPLATFORM") != "espressif8266":
    Return()

framework = Path(
    env.PioPlatform().get_package_dir("framework-arduinoespressif8266")
)
manifest = framework / "package.json"
source_root = framework / "libraries" / "ESP8266WiFi" / "src"
header = source_root / "WiFiClientSecureBearSSL.h"
source = source_root / "WiFiClientSecureBearSSL.cpp"

expected_version = "3.30102.0"
expected_hashes = {
    header: "85d80b7c7e3c6124cc44fd842acbfcb2a0bab388577adad2db29ebb741208858",
    source: "c993ccd21b962c67e3e07ff4747ab4d812ecbf619223aaf54443e971cfd5c487",
}
expected_patched_hashes = {
    header: "f75163ba7dec18d72b624f8179072720be6a6765247f877c9a047cd86fdacc92",
    source: "00c7824d96f09535877656aba4a32d4f4cf5f0d9e9dda01ea8710ea7fb02aa27",
}
marker = "FLOVA_BEARSSL_NONBLOCKING_WRITE"


if not manifest.exists() or loads(manifest.read_text())["version"] != expected_version:
    raise RuntimeError(
        "Flova's ESP8266 BearSSL patch requires "
        f"framework-arduinoespressif8266@{expected_version}"
    )

contents = {path: path.read_text() for path in expected_hashes}
marked = {path: marker in text for path, text in contents.items()}
if all(marked.values()):
    for path, expected in expected_patched_hashes.items():
        actual = sha256(path.read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(
                f"Modified ESP8266 BearSSL patched source for {path.name}: {actual}"
            )
    Return()
if any(marked.values()):
    raise RuntimeError("ESP8266 BearSSL patch is only partially applied")

for path, expected in expected_hashes.items():
    actual = sha256(path.read_bytes()).hexdigest()
    if actual != expected:
        raise RuntimeError(
            f"Unexpected ESP8266 BearSSL source hash for {path.name}: {actual}"
        )

header_text = contents[header]
ctx_anchor = """    int availableForWrite() override;

    // Allow sessions to be saved/restored automatically to a memory area
"""
ctx_replacement = """    int availableForWrite() override;

    // FLOVA_BEARSSL_NONBLOCKING_WRITE: cooperative Link I/O. These methods
    // never wait for a TCP acknowledgement.
    int writeNonBlocking(const uint8_t *buf, size_t size);
    bool pollNonBlocking();

    // Allow sessions to be saved/restored automatically to a memory area
"""
wrapper_anchor = """    int availableForWrite() override { return _ctx->availableForWrite(); }
    int read() override { return _ctx->read(); }
"""
wrapper_replacement = """    int availableForWrite() override { return _ctx->availableForWrite(); }
    int writeNonBlocking(const uint8_t *buf, size_t size) { return _ctx->writeNonBlocking(buf, size); }
    bool pollNonBlocking() { return _ctx->pollNonBlocking(); }
    void setNoDelay(bool nodelay) { _ctx->setNoDelay(nodelay); }
    int read() override { return _ctx->read(); }
"""
if header_text.count(ctx_anchor) != 1 or header_text.count(wrapper_anchor) != 1:
    raise RuntimeError("ESP8266 BearSSL header hooks were not found exactly once")
header_text = header_text.replace(ctx_anchor, ctx_replacement)
header_text = header_text.replace(wrapper_anchor, wrapper_replacement)

source_text = contents[source]
write_anchor = """size_t WiFiClientSecureCtx::write(const uint8_t *buf, size_t size) {
  return _write(buf, size, false);
}
"""
write_replacement = """// FLOVA_BEARSSL_NONBLOCKING_WRITE: accept plaintext and advance BearSSL/lwIP
// without invoking WiFiClient::flush(), which waits for peer TCP ACKs.
int WiFiClientSecureCtx::writeNonBlocking(const uint8_t *buf, size_t size) {
  if (!buf || !size || !_engineConnected()) {
    return -1;
  }
  const int ready = _run_until(BR_SSL_SENDAPP, false);
  if (ready < 0) {
    return -1;
  }
  if (!(br_ssl_engine_current_state(_eng) & BR_SSL_SENDAPP)) {
    return 0;
  }
  size_t capacity = 0;
  unsigned char *destination = br_ssl_engine_sendapp_buf(_eng, &capacity);
  const size_t accepted = size < capacity ? size : capacity;
  memcpy(destination, buf, accepted);
  br_ssl_engine_sendapp_ack(_eng, accepted);
  br_ssl_engine_flush(_eng, 0);
  (void)_run_until(BR_SSL_SENDAPP | BR_SSL_RECVAPP, false);
  return static_cast<int>(accepted);
}

bool WiFiClientSecureCtx::pollNonBlocking() {
  return _engineConnected() &&
         _run_until(BR_SSL_SENDAPP | BR_SSL_RECVAPP, false) >= 0;
}

size_t WiFiClientSecureCtx::write(const uint8_t *buf, size_t size) {
  return _write(buf, size, false);
}
"""
zero_window_anchor = """      wlen = WiFiClient::write(buf, len);
      if (wlen <= 0) {
"""
zero_window_replacement = """      if (!len && !blocking) {
        ++no_work;
        continue;
      }
      wlen = WiFiClient::write(buf, len);
      if (wlen <= 0) {
"""
recvapp_anchor = """    if (state & BR_SSL_RECVAPP) {
      DEBUG_BSSL("_run_until: Fatal protocol state\\n");
      return -1;
    }
"""
recvapp_replacement = """    if (state & BR_SSL_RECVAPP) {
      if (!blocking) {
        return 1;
      }
      DEBUG_BSSL("_run_until: Fatal protocol state\\n");
      return -1;
    }
"""
idle_anchor = """  // We only get here if we ran through the loop without getting anything done
  return -1;
}
"""
idle_replacement = """  // A bounded non-blocking poll may legitimately make no progress.
  return blocking ? -1 : 1;
}
"""
for anchor, replacement, name in (
    (write_anchor, write_replacement, "write API"),
    (zero_window_anchor, zero_window_replacement, "zero-window handling"),
    (recvapp_anchor, recvapp_replacement, "receive backpressure"),
    (idle_anchor, idle_replacement, "non-blocking idle result"),
):
    if source_text.count(anchor) != 1:
        raise RuntimeError(f"ESP8266 BearSSL {name} hook was not found exactly once")
    source_text = source_text.replace(anchor, replacement)

header.write_text(header_text)
source.write_text(source_text)
