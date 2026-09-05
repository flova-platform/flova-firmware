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
wifi_header = source_root / "WiFiClient.h"
wifi_source = source_root / "WiFiClient.cpp"
context_header = source_root / "include" / "ClientContext.h"

expected_version = "3.30102.0"
expected_hashes = {
    header: "85d80b7c7e3c6124cc44fd842acbfcb2a0bab388577adad2db29ebb741208858",
    source: "c993ccd21b962c67e3e07ff4747ab4d812ecbf619223aaf54443e971cfd5c487",
}
expected_patched_hashes = {
    header: "f75163ba7dec18d72b624f8179072720be6a6765247f877c9a047cd86fdacc92",
    source: "00c7824d96f09535877656aba4a32d4f4cf5f0d9e9dda01ea8710ea7fb02aa27",
}
expected_connect_patched_hashes = {
    header: "f4a54e82fd74737982a5dc7ab0a6ac31b373d60242dcd6b79f8a881754d3e21c",
    source: "ba72d0704815c503bd7489033bccc25270f77a7474f28de691ffc06069920f1f",
    wifi_header: "4cbe824b5363beaf2e594a59e3f51f99639b816e933e714920960d78a3a99420",
    wifi_source: "ce6e4c21a5a9ed32d3cc6e6433091c0035340552e0dcc9532467bfb15e194cbc",
    context_header: "43289a317a92e619ceabc1ff4c4b9218c29e37704908ceb02487d4fd5e8e4df8",
}
marker = "FLOVA_BEARSSL_NONBLOCKING_WRITE"
connect_marker = "FLOVA_BEARSSL_NONBLOCKING_CONNECT"


if not manifest.exists() or loads(manifest.read_text())["version"] != expected_version:
    raise RuntimeError(
        "Flova's ESP8266 BearSSL patch requires "
        f"framework-arduinoespressif8266@{expected_version}"
    )

contents = {path: path.read_text() for path in expected_hashes}
marked = {path: marker in text for path, text in contents.items()}
if all(marked.values()):
    if connect_marker in contents[header] and connect_marker in contents[source]:
        for path, expected in expected_connect_patched_hashes.items():
            actual = sha256(path.read_bytes()).hexdigest()
            if actual != expected:
                raise RuntimeError(
                    f"Modified ESP8266 non-blocking source for {path.name}: {actual}"
                )
        Return()
    for path, expected in expected_patched_hashes.items():
        actual = sha256(path.read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(
                f"Modified ESP8266 BearSSL patched source for {path.name}: {actual}"
            )
    script_path = Path(__file__).resolve()
    connect_patch = script_path.read_text().rsplit(
        "# The normal runtime must also advance TCP and TLS connection setup without",
        1,
    )[1]
    exec(connect_patch)
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

# The normal runtime must also advance TCP and TLS connection setup without
# waiting inside the developer-owned Arduino loop. Keep this patch pinned to
# the exact framework version above, just like the cooperative write hooks.
wifi_header_text = wifi_header.read_text()
wifi_source_text = wifi_source.read_text()
context_text = context_header.read_text()
header_text = header.read_text()
source_text = source.read_text()

if connect_marker in header_text and connect_marker in source_text:
    Return()

wifi_header_anchor = """  virtual int connect(const String& host, uint16_t port);
  virtual size_t write(uint8_t) override;
"""
wifi_header_replacement = """  virtual int connect(const String& host, uint16_t port);

  // FLOVA_BEARSSL_NONBLOCKING_CONNECT: cooperative TCP connection setup.
  bool startConnectNonBlocking(IPAddress ip, uint16_t port);
  int pollConnectNonBlocking();

  virtual size_t write(uint8_t) override;
"""
if wifi_header_text.count(wifi_header_anchor) != 1:
    raise RuntimeError("ESP8266 WiFiClient declaration anchor changed")
wifi_header_text = wifi_header_text.replace(wifi_header_anchor, wifi_header_replacement)

context_anchor = """    size_t availableForWrite() const
    {
"""
context_replacement = """    // FLOVA_BEARSSL_NONBLOCKING_CONNECT: start lwIP once and poll its
    // callback-owned state without esp_delay().
    int connectStart(ip_addr_t* addr, uint16_t port)
    {
        err_t err = tcp_connect(_pcb, addr, port, &ClientContext::_s_connected);
        if (err != ERR_OK) return 0;
        _connect_pending = true;
        _op_start_time = millis();
        return 1;
    }

    int connectPoll()
    {
        if (_connect_pending) {
            if (!_is_timeout()) return 0;
            _connect_pending = false;
            abort();
            return -1;
        }
        return _pcb && state() == ESTABLISHED ? 1 : -1;
    }

    size_t availableForWrite() const
    {
"""
if context_text.count(context_anchor) != 1:
    raise RuntimeError("ESP8266 ClientContext connection anchor changed")
context_text = context_text.replace(context_anchor, context_replacement)

wifi_source_anchor = """int WiFiClient::connect(const char* host, uint16_t port)
{
"""
wifi_source_replacement = """// FLOVA_BEARSSL_NONBLOCKING_CONNECT: caller resolves DNS, then starts
// and polls the lwIP TCP callback state.
bool WiFiClient::startConnectNonBlocking(IPAddress ip, uint16_t port)
{
    if (_client) {
        stop(0);
        _client->unref();
        _client = nullptr;
    }
    tcp_pcb* pcb = tcp_new();
    if (!pcb) return false;
    _client = new ClientContext(pcb, nullptr, nullptr);
    if (!_client) {
        tcp_abort(pcb);
        return false;
    }
    _client->ref();
    _client->setTimeout(_timeout);
    return _client->connectStart(ip, port) != 0;
}

int WiFiClient::pollConnectNonBlocking()
{
    if (!_client) return -1;
    const int result = _client->connectPoll();
    if (result > 0) {
        setSync(defaultSync);
        setNoDelay(defaultNoDelay);
    } else if (result < 0) {
        _client->unref();
        _client = nullptr;
    }
    return result;
}

int WiFiClient::connect(const char* host, uint16_t port)
{
"""
if wifi_source_text.count(wifi_source_anchor) != 1:
    raise RuntimeError("ESP8266 WiFiClient implementation anchor changed")
wifi_source_text = wifi_source_text.replace(wifi_source_anchor, wifi_source_replacement)

ctx_public_anchor = """  protected:
    bool _connectSSL(const char *hostName); // Do initial SSL handshake
"""
ctx_public_replacement = """    // FLOVA_BEARSSL_NONBLOCKING_CONNECT: TCP and BearSSL are advanced
    // separately by the board-owned connection state machine.
    bool startTcpNonBlocking(IPAddress ip, uint16_t port) { return WiFiClient::startConnectNonBlocking(ip, port); }
    int pollTcpNonBlocking() { return WiFiClient::pollConnectNonBlocking(); }
    bool startTlsNonBlocking(const char *hostName) { return _connectSSL(hostName, false); }
    int pollTlsNonBlocking();

  protected:
    bool _connectSSL(const char *hostName, bool blocking = true); // Do initial SSL handshake
"""
wrapper_anchor = """    int connect(const char* name, uint16_t port) override { return _ctx->connect(name, port); }

    uint8_t connected() override { return _ctx->connected(); }
"""
wrapper_replacement = """    int connect(const char* name, uint16_t port) override { return _ctx->connect(name, port); }
    bool startTcpNonBlocking(IPAddress ip, uint16_t port) { return _ctx->startTcpNonBlocking(ip, port); }
    int pollTcpNonBlocking() { return _ctx->pollTcpNonBlocking(); }
    bool startTlsNonBlocking(const char *hostName) { return _ctx->startTlsNonBlocking(hostName); }
    int pollTlsNonBlocking() { return _ctx->pollTlsNonBlocking(); }

    uint8_t connected() override { return _ctx->connected(); }
"""
if header_text.count(ctx_public_anchor) != 1 or header_text.count(wrapper_anchor) != 1:
    raise RuntimeError("ESP8266 BearSSL connection declaration anchor changed")
header_text = header_text.replace(ctx_public_anchor, ctx_public_replacement)
header_text = header_text.replace(wrapper_anchor, wrapper_replacement)

source_text = source_text.replace(
    "bool WiFiClientSecureCtx::_connectSSL(const char* hostName) {",
    "bool WiFiClientSecureCtx::_connectSSL(const char* hostName, bool blocking) {",
    1,
)
handshake_anchor = """  auto ret = _wait_for_handshake();
#ifdef DEBUG_ESP_SSL
"""
handshake_replacement = """  if (!blocking) return true;
  auto ret = _wait_for_handshake();
#ifdef DEBUG_ESP_SSL
"""
if source_text.count(handshake_anchor) != 1:
    raise RuntimeError("ESP8266 BearSSL handshake anchor changed")
source_text = source_text.replace(handshake_anchor, handshake_replacement)

poll_anchor = """// Called by connect() to do the actual SSL setup and handshake.
// Returns if the SSL handshake succeeded.
"""
poll_replacement = """// FLOVA_BEARSSL_NONBLOCKING_CONNECT: advance at most one bounded BearSSL
// engine poll and report 0=in progress, 1=complete, -1=failed.
int WiFiClientSecureCtx::pollTlsNonBlocking() {
  if (!_clientConnected() || !ctx_present()) return -1;
  const int result = _run_until(BR_SSL_SENDAPP, false);
  if (result < 0) return -1;
  const unsigned state = br_ssl_engine_current_state(_eng);
  if (!(state & BR_SSL_SENDAPP)) return 0;
  _handshake_done = true;
  _x509_minimal = nullptr;
  _x509_insecure = nullptr;
  _x509_knownkey = nullptr;
  _timeout = 5000;
  return 1;
}

// Called by connect() to do the actual SSL setup and handshake.
// Returns if the SSL handshake succeeded.
"""
if source_text.count(poll_anchor) != 1:
    raise RuntimeError("ESP8266 BearSSL poll insertion anchor changed")
source_text = source_text.replace(poll_anchor, poll_replacement)

wifi_header.write_text(wifi_header_text)
wifi_source.write_text(wifi_source_text)
context_header.write_text(context_text)
header.write_text(header_text)
source.write_text(source_text)
