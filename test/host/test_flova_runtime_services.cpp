#include <assert.h>
#include <string.h>

#include <FlovaProvisioningAdapter.h>
#include <FlovaRuntimeServices.h>
#include <FlovaConfigurationActivation.h>

static_assert(sizeof(flova::ConfigurationActivation) <= 8,
              "configuration activation exceeded its fixed state budget");

namespace {

struct ActivationLink {
  bool accept = false;
  bool ready = false;
  bool failed = false;
  unsigned reports = 0;
  unsigned drains = 0;
  unsigned disconnects = 0;
  void pollBootstrap() {}
  bool publishConfigurationReport(const int& report) {
    assert(report == 2);
    ++reports;
    return accept;
  }
  void beginMaintenance() { ++drains; }
  bool maintenanceReady() { return ready; }
  bool maintenanceFailed() const { return failed; }
  void disconnect() { ++disconnects; }
};

void verifyConfigurationActivation() {
  flova::ConfigurationActivation activation;
  ActivationLink link;
  const int generation = 2;
  assert(!activation.run(link, generation, 0));
  activation.begin(100);
  assert(!activation.run(link, generation, 101));
  assert(link.drains == 0 && link.disconnects == 0);
  link.accept = true;
  assert(!activation.run(link, generation, 102));
  assert(link.drains == 1 && link.reports == 2);
  assert(!activation.run(link, generation, 103));
  assert(link.reports == 2);  // Never enqueue the accepted ACK twice.
  link.ready = true;
  assert(activation.run(link, generation, 104));
  assert(!activation.active() && !activation.failed());

  link = ActivationLink();
  activation.begin(UINT32_MAX - 1000);
  assert(!activation.run(link, generation, 3998));
  assert(activation.run(link, generation, 3999));
  assert(activation.failed() && link.disconnects == 1 && link.drains == 0);

  link = ActivationLink();
  link.accept = true;
  link.ready = true;
  link.failed = true;
  activation.begin(0);
  assert(activation.run(link, generation, 1));
  assert(activation.failed());
}

class TestProvisioning final : public FlovaProvisioningAdapter {
 public:
  TestProvisioning() : started(false), stopped(false) {}
  bool startProvisioning() override { started = true; return true; }
  bool stopProvisioning() override { stopped = true; return true; }
  bool stopAfterNetworkConnected() const override { return true; }
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
  verifyConfigurationActivation();
  FlovaProvisioningAdapter passiveProvisioning;
  assert(!passiveProvisioning.stopAfterNetworkConnected());

  TestProvisioning provisioning;
  TestNetwork network;
  TestTlsClock tlsClock;
  TestIdentity identity;

  assert(provisioning.startProvisioning());
  assert(provisioning.started);
  assert(provisioning.stopAfterNetworkConnected());
  assert(!network.began);

  assert(network.begin());
  tlsClock.loop(network.connected());
  assert(provisioning.stopProvisioning());

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
