#include <assert.h>
#include <string.h>

#include <FlovaProvisioningAdapter.h>
#include <FlovaRuntimeServices.h>

namespace {

class TestProvisioning final : public FlovaProvisioningAdapter {
 public:
  TestProvisioning() : started(false), stopped(false) {}
  bool startProvisioning() override { started = true; return true; }
  bool stopProvisioning() override { stopped = true; return true; }
  bool started;
  bool stopped;
};

class TestNetwork final : public FlovaNetworkRuntime {
 public:
  TestNetwork() : began(false), online(false) {}
  bool begin() override { began = true; online = true; return true; }
  bool connected() const override { return online; }
  bool began;
  bool online;
};

class TestTlsClock final : public FlovaTlsClockBootstrap {
 public:
  TestTlsClock() : polled(false), networkWasConnected(false) {}
  void loop(bool connected) override { polled = true; networkWasConnected = connected; }
  bool ready() const override { return networkWasConnected; }
  bool polled;
  bool networkWasConnected;
};

class TestIdentity final : public FlovaBoardIdentity {
 public:
  bool hardwareId(char* output, size_t capacity) const override {
    static const char kId[] = "test-board";
    if (capacity < sizeof(kId)) return false;
    memcpy(output, kId, sizeof(kId));
    return true;
  }
  const char* firmwareTarget() const override { return "test-target"; }
};

}  // namespace

int main() {
  TestProvisioning provisioning;
  TestNetwork network;
  TestTlsClock tlsClock;
  TestIdentity identity;

  assert(provisioning.startProvisioning());
  assert(provisioning.started);
  assert(!network.began);

  assert(provisioning.stopProvisioning());
  assert(network.begin());
  tlsClock.loop(network.connected());

  assert(provisioning.stopped);
  assert(network.connected());
  assert(tlsClock.polled);
  assert(tlsClock.ready());

  char hardwareId[16] = {};
  assert(identity.hardwareId(hardwareId, sizeof(hardwareId)));
  assert(strcmp(hardwareId, "test-board") == 0);
  assert(strcmp(identity.firmwareTarget(), "test-target") == 0);
  return 0;
}
