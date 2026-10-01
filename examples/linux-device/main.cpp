#include <FlovaLinux.h>

#include <FlovaCustomCode.h>

// Linux applications provide the Link implementation for their transport or
// gateway. The POSIX services below are ready to inject into flova::Device.
class LinuxLink final : public flova::Link {
 public:
  bool begin() override { return true; }
  bool connected() const override { return true; }
  bool send(const flova::Message&) override { return true; }
  void poll() override {}
  void setReceiver(flova::MessageReceiver receiver, void* context) override {
    receiver_ = receiver;
    context_ = context;
  }
  uint32_t messageNonce() const override { return 1; }
  bool bindDatastreams(const char* const*, size_t count,
                      DatastreamId* ids) override {
    if (count > 1 || (count && !ids)) return false;
    if (count) ids[0] = 1;
    return true;
  }

 private:
  flova::MessageReceiver receiver_ = nullptr;
  void* context_ = nullptr;
};

int main() {
  LinuxLink link;
  flova_linux::FileStorage storage("/var/lib/flova/device");
  flova_linux::Clock clock;
  flova_linux::Logger logger;
  flova::Device device(link, storage, clock, logger);
  auto relay = FLOVA_DATASTREAM(device, bool, "relay");
  if (!device.begin()) return 1;
  relay.write(true);
  for (;;) device.run();
}
