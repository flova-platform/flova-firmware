#include <FlovaLinux.h>

#include <Arduino.h>

#include <FlovaArduinoPlatform.h>
#include <FlovaWs.h>
#include <adapters/ArduinoFlovaLink.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <cstdlib>
#include <atomic>
#include <thread>

namespace {

class Entropy final : public FlovaEntropySource {
 public:
  uint8_t byte() override {
    uint8_t value = 0;
    if (RAND_bytes(&value, sizeof(value)) == 1) return value;
    return static_cast<uint8_t>(millis() ^ reinterpret_cast<uintptr_t>(this));
  }
};

class LinuxPlatform final : public FlovaArduinoPlatform {
 public:
  ~LinuxPlatform() override { closeLink(); }

  bool connected() override { return tlsReady_; }
  int available() override { fillRx(); return static_cast<int>((rxTail_ + sizeof(rx_) - rxHead_) % sizeof(rx_)); }
  int read() override { return readByte(); }
  bool linkClosed() const override { return socket_ < 0; }
  bool beginLink() override {
    SSL_library_init();
    SSL_load_error_strings();
    return true;
  }

  bool startLink(const char* host, uint16_t port) override {
    closeLink();
    if (!host || !*host || !port) return false;
    strncpy(host_, host, sizeof(host_) - 1);
    addressLength_ = 0;
    resolverDone_ = false;
    resolverFailed_ = false;
    resolverRunning_ = true;
    resolver_ = std::thread([this, port]() {
      char service[8] = {};
      snprintf(service, sizeof(service), "%u", static_cast<unsigned>(port));
      struct addrinfo hints = {};
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_family = AF_UNSPEC;
      struct addrinfo* addresses = nullptr;
      if (getaddrinfo(host_, service, &hints, &addresses) != 0 || !addresses) {
        resolverFailed_ = true;
        resolverDone_ = true;
        resolverRunning_ = false;
        return;
      }
      memcpy(&address_, addresses->ai_addr, addresses->ai_addrlen);
      addressLength_ = static_cast<socklen_t>(addresses->ai_addrlen);
      addressFamily_ = addresses->ai_family;
      addressType_ = addresses->ai_socktype;
      addressProtocol_ = addresses->ai_protocol;
      freeaddrinfo(addresses);
      resolverDone_ = true;
      resolverRunning_ = false;
    });
    return true;
  }

  FlovaLinkOpenStatus pollLink() override {
    if (resolverRunning_ || !resolverDone_) return FlovaLinkOpenStatus::InProgress;
    if (resolver_.joinable()) resolver_.join();
    if (resolverFailed_) return FlovaLinkOpenStatus::Failed;
    if (socket_ < 0 && addressLength_) {
      socket_ = socket(addressFamily_, addressType_, addressProtocol_);
      if (socket_ < 0) return FlovaLinkOpenStatus::Failed;
      const int flags = fcntl(socket_, F_GETFL, 0);
      fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
      const int result = connect(socket_, reinterpret_cast<struct sockaddr*>(&address_), addressLength_);
      if (result != 0 && errno != EINPROGRESS) {
        closeLink();
        return FlovaLinkOpenStatus::Failed;
      }
      connecting_ = result != 0;
    }
    if (socket_ < 0) return FlovaLinkOpenStatus::Failed;
    if (connecting_) {
      int error = 0;
      socklen_t length = sizeof(error);
      if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &length) != 0)
        return FlovaLinkOpenStatus::InProgress;
      if (error == EINPROGRESS || error == EALREADY) return FlovaLinkOpenStatus::InProgress;
      if (error != 0) {
        closeLink();
        return FlovaLinkOpenStatus::Failed;
      }
      connecting_ = false;
    }
    if (!ssl_) {
      ctx_ = SSL_CTX_new(TLS_client_method());
      if (!ctx_) return FlovaLinkOpenStatus::Failed;
      SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
      SSL_CTX_set_default_verify_paths(ctx_);
      SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
      ssl_ = SSL_new(ctx_);
      if (!ssl_) return FlovaLinkOpenStatus::Failed;
      SSL_set_fd(ssl_, socket_);
      SSL_set_tlsext_host_name(ssl_, host_);
      SSL_set_verify(ssl_, SSL_VERIFY_PEER, nullptr);
      X509_VERIFY_PARAM* verify = SSL_get0_param(ssl_);
      if (!verify || X509_VERIFY_PARAM_set1_host(verify, host_, 0) != 1) {
        closeLink();
        return FlovaLinkOpenStatus::Failed;
      }
    }
    const int result = SSL_connect(ssl_);
    if (result == 1) {
      tlsReady_ = true;
      return FlovaLinkOpenStatus::Connected;
    }
    const int error = SSL_get_error(ssl_, result);
    if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
      return FlovaLinkOpenStatus::InProgress;
    closeLink();
    return FlovaLinkOpenStatus::Failed;
  }

  void closeLink() override {
    if (resolver_.joinable()) resolver_.join();
    if (ssl_) {
      SSL_shutdown(ssl_);
      SSL_free(ssl_);
      ssl_ = nullptr;
    }
    if (ctx_) {
      SSL_CTX_free(ctx_);
      ctx_ = nullptr;
    }
    if (socket_ >= 0) {
      close(socket_);
      socket_ = -1;
    }
    connecting_ = false;
    tlsReady_ = false;
    rxHead_ = rxTail_ = 0;
    txLength_ = txOffset_ = 0;
  }

  bool submitLinkWrite(const uint8_t* data, size_t length) override {
    if (!tlsReady_ || !data || !length || length > sizeof(tx_)) return false;
    if (txLength_ != txOffset_) return false;
    memcpy(tx_, data, length);
    txLength_ = length;
    txOffset_ = 0;
    return true;
  }

  bool serviceLinkWrite() override {
    if (!tlsReady_ || txOffset_ == txLength_) return true;
    const int result = SSL_write(ssl_, tx_ + txOffset_,
                                 static_cast<int>(txLength_ - txOffset_));
    if (result > 0) {
      txOffset_ += static_cast<size_t>(result);
      if (txOffset_ == txLength_) txLength_ = txOffset_ = 0;
      return true;
    }
    const int error = SSL_get_error(ssl_, result);
    if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return true;
    closeLink();
    return false;
  }

  bool linkWriteBusy() const override { return txLength_ != txOffset_; }
  flova::OtaInstallResult installOta(const FlovaLinkOtaOffer&) override {
    return flova::OtaInstallResult::ResourceUnavailable;
  }

  int readByte() {
    if (!available()) return -1;
    const uint8_t value = rx_[rxHead_];
    rxHead_ = (rxHead_ + 1) % sizeof(rx_);
    return value;
  }

 private:
  void fillRx() {
    if (!tlsReady_) return;
    while (true) {
      const size_t freeBytes = (rxHead_ + sizeof(rx_) - rxTail_ - 1) % sizeof(rx_);
      if (!freeBytes) return;
      const size_t contiguous = rxTail_ >= rxHead_ ? sizeof(rx_) - rxTail_ : rxHead_ - rxTail_ - 1;
      const int result = SSL_read(ssl_, rx_ + rxTail_, static_cast<int>(contiguous));
      if (result > 0) {
        rxTail_ = (rxTail_ + static_cast<size_t>(result)) % sizeof(rx_);
        continue;
      }
      const int error = SSL_get_error(ssl_, result);
      if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return;
      closeLink();
      return;
    }
  }

  int socket_ = -1;
  bool connecting_ = false;
  bool tlsReady_ = false;
  char host_[256] = {};
  std::thread resolver_;
  std::atomic<bool> resolverRunning_{false};
  std::atomic<bool> resolverDone_{true};
  std::atomic<bool> resolverFailed_{false};
  struct sockaddr_storage address_ = {};
  socklen_t addressLength_ = 0;
  int addressFamily_ = AF_UNSPEC;
  int addressType_ = SOCK_STREAM;
  int addressProtocol_ = 0;
  SSL_CTX* ctx_ = nullptr;
  SSL* ssl_ = nullptr;
  uint8_t rx_[16384] = {};
  size_t rxHead_ = 0;
  size_t rxTail_ = 0;
  uint8_t tx_[526] = {};
  size_t txLength_ = 0;
  size_t txOffset_ = 0;
};

}  // namespace

struct FlovaLinux::Impl::State {
  explicit State(const Config& input)
      : config(input),
        storage(input.stateDirectory),
        device(link, storage, clock, logger) {}

  Config config;
  Entropy entropy;
  LinuxPlatform platform;
  ArduinoFlovaLink link{platform, entropy};
  flova_linux::FileStorage storage;
  flova_linux::Clock clock;
  flova_linux::Logger logger;
  flova::Device device;
  bool started = false;
  const char* error = "";
};

FlovaLinux::Impl::Impl(const Config& config) : state_(new State(config)) {}
FlovaLinux::Impl::~Impl() { delete state_; }

bool FlovaLinux::Impl::begin() {
  State& state = *state_;
  if (!state.config.url || !state.config.deviceId || !state.config.secret ||
      !state.link.configure(state.config.url, state.config.deviceId,
                            state.config.secret)) {
    state.error = "invalid_linux_device_config";
    return false;
  }
  state.link.setConnectionAllowed(true);
  if (!state.device.begin()) {
    state.error = "device_begin_failed";
    return false;
  }
  state.started = true;
  return true;
}

void FlovaLinux::Impl::run() {
  if (state_->started) state_->device.run();
}

bool FlovaLinux::Impl::connected() const { return state_->link.connected(); }
bool FlovaLinux::Impl::ready() const { return state_->device.ready(); }
bool FlovaLinux::Impl::running() const { return state_->started; }
const char* FlovaLinux::Impl::lastError() const { return state_->error; }

template <typename T>
flova::WriteResult FlovaLinux::Impl::write(const char* key, const T& value) {
  return state_->device.datastream<T>(key).write(value);
}

template <typename T>
bool FlovaLinux::Impl::hasValue(const char* key) const {
  return const_cast<flova::Device&>(state_->device).datastream<T>(key).hasValue();
}

template <typename T>
T FlovaLinux::Impl::value(const char* key) const {
  return const_cast<flova::Device&>(state_->device).datastream<T>(key).value();
}

template <typename T>
flova::WriteResult FlovaLinux::Impl::report(const char* key, const T& value,
                                            flova::Origin origin) {
  return state_->device.datastream<T>(key).report(value, origin);
}

template <typename T>
bool FlovaLinux::Impl::onWrite(const char* key, flova::ValueType,
                               FlovaDatastreamBinding<T>& binding) {
  auto stream = state_->device.datastream<T>(key);
  switch (binding.kind) {
    case FlovaDatastreamHandlerKind::Result:
      stream.onWrite(binding.handler.result);
      return true;
    case FlovaDatastreamHandlerKind::ResultWithContext:
      stream.onWrite(binding.handler.resultContext, binding.context);
      return true;
    case FlovaDatastreamHandlerKind::Void:
      stream.onWrite(binding.handler.voidHandler);
      return true;
    case FlovaDatastreamHandlerKind::VoidWithContext:
      stream.onWrite(binding.handler.voidContext, binding.context);
      return true;
    default:
      return false;
  }
}

template flova::WriteResult FlovaLinux::Impl::write<bool>(const char*, const bool&);
template flova::WriteResult FlovaLinux::Impl::write<int64_t>(const char*, const int64_t&);
template flova::WriteResult FlovaLinux::Impl::write<float>(const char*, const float&);
template flova::WriteResult FlovaLinux::Impl::write<double>(const char*, const double&);
template flova::WriteResult FlovaLinux::Impl::write<flova::Text>(const char*, const flova::Text&);
template bool FlovaLinux::Impl::hasValue<bool>(const char*) const;
template bool FlovaLinux::Impl::hasValue<int64_t>(const char*) const;
template bool FlovaLinux::Impl::hasValue<float>(const char*) const;
template bool FlovaLinux::Impl::hasValue<double>(const char*) const;
template bool FlovaLinux::Impl::hasValue<flova::Text>(const char*) const;
template bool FlovaLinux::Impl::value<bool>(const char*) const;
template int64_t FlovaLinux::Impl::value<int64_t>(const char*) const;
template float FlovaLinux::Impl::value<float>(const char*) const;
template double FlovaLinux::Impl::value<double>(const char*) const;
template flova::Text FlovaLinux::Impl::value<flova::Text>(const char*) const;
template flova::WriteResult FlovaLinux::Impl::report<bool>(const char*, const bool&, flova::Origin);
template flova::WriteResult FlovaLinux::Impl::report<int64_t>(const char*, const int64_t&, flova::Origin);
template flova::WriteResult FlovaLinux::Impl::report<float>(const char*, const float&, flova::Origin);
template flova::WriteResult FlovaLinux::Impl::report<double>(const char*, const double&, flova::Origin);
template flova::WriteResult FlovaLinux::Impl::report<flova::Text>(const char*, const flova::Text&, flova::Origin);
template bool FlovaLinux::Impl::onWrite<bool>(const char*, flova::ValueType, FlovaDatastreamBinding<bool>&);
template bool FlovaLinux::Impl::onWrite<int64_t>(const char*, flova::ValueType, FlovaDatastreamBinding<int64_t>&);
template bool FlovaLinux::Impl::onWrite<float>(const char*, flova::ValueType, FlovaDatastreamBinding<float>&);
template bool FlovaLinux::Impl::onWrite<double>(const char*, flova::ValueType, FlovaDatastreamBinding<double>&);
template bool FlovaLinux::Impl::onWrite<flova::Text>(const char*, flova::ValueType, FlovaDatastreamBinding<flova::Text>&);

FlovaLinux::FlovaLinux()
    : impl_(new Impl({getenv("FLOVA_LINK_URL"), getenv("FLOVA_DEVICE_ID"),
                      getenv("FLOVA_DEVICE_SECRET"),
                      getenv("FLOVA_STATE_DIRECTORY") ?: "/var/lib/flova/device"})) {}
FlovaLinux::FlovaLinux(const Config& config) : impl_(new Impl(config)) {}
FlovaLinux::~FlovaLinux() { delete impl_; }
bool FlovaLinux::begin() { return impl_->begin(); }
void FlovaLinux::run() { impl_->run(); }
bool FlovaLinux::connected() const { return impl_->connected(); }
bool FlovaLinux::ready() const { return impl_->ready(); }
bool FlovaLinux::running() const { return impl_->running(); }
const char* FlovaLinux::lastError() const { return impl_->lastError(); }
