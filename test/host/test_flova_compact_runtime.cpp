#include <FlovaDevice.h>
#include <assert.h>
#include <map>
#include <string>
#include <vector>

struct Storage : flova::Storage {
  std::map<std::string, std::vector<uint8_t>> records;
  bool failRead = false, failWrite = false;
  unsigned writes = 0;
  bool read(const char* key, void* out, size_t size) override {
    const auto it = records.find(key);
    if (failRead || it == records.end() || it->second.size() != size) return false;
    memcpy(out, it->second.data(), size); return true;
  }
  bool write(const char* key, const void* data, size_t size) override {
    if (failWrite) return false;
    const auto* bytes = static_cast<const uint8_t*>(data);
    records[key] = std::vector<uint8_t>(bytes, bytes + size); ++writes; return true;
  }
  bool remove(const char* key) override { records.erase(key); return true; }
};
struct Clock : flova::Clock {
  uint64_t now = 1;
  uint64_t milliseconds() const override { return now; }
};
struct Logger : flova::Logger { void log(const char*) override {} };
struct Link : flova::Link {
  flova::MessageReceiver receiver = nullptr;
  void* context = nullptr;
  std::vector<flova::Message> sent;
  bool online = true, paused = false;
  bool applicationPaused() const override { return paused; }
  bool begin() override { return true; }
  bool connected() const override { return online; }
  void poll() override {}
  uint32_t messageNonce() const override { return 123; }
  void setReceiver(flova::MessageReceiver r, void* c) override { receiver = r; context = c; }
  bool send(const flova::Message& m) override { if (!online) return false; if (m.kind == flova::MessageKind::StateUpdate) sent.push_back(m); return true; }
  bool bindDatastreamKeys(flova::DatastreamKeyReader reader, void* ctx,
                          size_t count, DatastreamId* ids) override {
    char key[49];
    for (size_t i = 0; i < count; ++i) {
      if (!reader(ctx, i, key, sizeof(key)) || strlen(key) != 48) return false;
      assert(key[0] == static_cast<char>('a' + i / 26) && key[1] == static_cast<char>('a' + i % 26));
      ids[i] = static_cast<DatastreamId>(i + 1);
    }
    return true;
  }
  void ack(uint64_t id) { flova::Message m; m.kind = flova::MessageKind::Acknowledgement; m.messageId = id; receiver(context, m); }
};
static unsigned hardwareWrites;
static flova::WriteResult writeText(flova::Text) { ++hardwareWrites; return flova::accept(); }
static flova::WriteResult writeInteger(int64_t) { ++hardwareWrites; return flova::accept(); }
static void keyFor(unsigned i, char (&key)[49]) {
  memset(key, 'k', 48); key[48] = 0; key[0] = 'a' + i / 26; key[1] = 'a' + i % 26;
}
int main() {
  Storage storage; Link link; Clock clock; Logger logger;
  flova::Device device(link, storage, clock, logger);
  char key[49]; char text[flova::kMaxText]; memset(text, 'v', sizeof(text)); text[sizeof(text) - 1] = 0;
  for (unsigned i = 0; i < 64; ++i) {
    keyFor(i, key);
    auto stream = device.datastream<flova::Text>(key);
    assert(stream.valid()); stream.onWrite(writeText);
    assert(stream.write(flova::Text(text)).accepted());
  }
  assert(device.datastreamCount() == 64 && hardwareWrites == 64);
  assert(!device.datastream<bool>("overflow").valid());
  assert(device.begin());
  device.run();
  assert(link.sent.size() == 4); // Window size, independent of stream count.
  const unsigned registrationWrites = storage.writes;
  keyFor(0, key);
  auto first = device.datastream<flova::Text>(key);
  const auto old = link.sent.front();
  assert(first.write(flova::Text("newer")).accepted());
  link.ack(old.messageId);
  assert(first.snapshot().dirty); // Old revision cannot acknowledge the new one.
  size_t acknowledged = 1;
  bool observed[65] = {}; observed[1] = true;
  for (unsigned turn = 0; turn < 100 && acknowledged < 65; ++turn) {
    while (acknowledged < link.sent.size()) {
      const auto message = link.sent[acknowledged++];
      observed[message.datastreamId] = true;
      link.ack(message.messageId);
    }
    device.run();
  }
  for (unsigned i = 1; i <= 64; ++i) assert(observed[i]);
  assert(!first.snapshot().dirty);
  link.paused = true;
  const unsigned pausedWrites = hardwareWrites;
  assert(!first.write(flova::Text("paused")).accepted());
  assert(hardwareWrites == pausedWrites && strcmp(first.value().c_str(), "newer") == 0);
  link.paused = false;
  assert(storage.writes == registrationWrites); // No flash writes per live sample.
  // Reading other descriptors evicts the cache. A failed read cannot create a
  // duplicate registration or discard the already registered live values.
  storage.failRead = true;
  keyFor(32, key);
  assert(!device.datastream<flova::Text>(key).valid());
  assert(device.datastreamCount() == 64);
  assert(strcmp(first.value().c_str(), "newer") == 0);

  Storage safetyStorage; Link safetyLink; Clock safetyClock;
  flova::Device safety(safetyLink, safetyStorage, safetyClock, logger);
  keyFor(0, key); auto number = safety.datastream<int64_t>(key); number.onWrite(writeInteger);
  keyFor(1, key); assert(safety.datastream<bool>(key).valid());
  assert(safety.begin());
  flova::config::Unit unit; unit.kind = flova::config::UnitKind::Safety;
  unit.data.safety.datastreamId = 1; unit.data.safety.policy = static_cast<flova::config::SafetyPolicy>(1);
  unit.data.safety.hasMinimum = true;
  unit.data.safety.minimum.kind = flova::config::ValueKind::Int64;
  unit.data.safety.minimum.data.integer = 10;
  assert(safety.applyConfigurationUnit(unit));
  assert(number.write(10).accepted());
  const unsigned before = hardwareWrites;
  assert(!number.write(9).accepted() && hardwareWrites == before);
  safetyStorage.failWrite = true;
  unit.data.safety.minimum.data.integer = 0;
  assert(!safety.applyConfigurationUnit(unit));
  assert(!number.write(9).accepted() && hardwareWrites == before);
  keyFor(1, key); assert(safety.datastream<bool>(key).valid());
  safetyStorage.failRead = true;
  assert(!number.write(11).accepted() && hardwareWrites == before);
  assert(number.value() == 10);
}
