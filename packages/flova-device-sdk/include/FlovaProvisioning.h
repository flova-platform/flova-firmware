#pragma once
#include "FlovaCore.h"

namespace flova {

enum class ProvisioningStatus : uint8_t { Idle, Bootstrapping, Committing, Ready, Failed };
enum class ProvisioningPoll : uint8_t { Pending, Complete, Failed };

struct ProvisioningRequest {
  char engineUrl[kMaxText], token[kMaxText], hardwareId[kMaxText], boardType[kMaxText], firmwareTarget[kMaxText];
  ProvisioningRequest() { engineUrl[0] = token[0] = hardwareId[0] = boardType[0] = firmwareTarget[0] = 0; }
};
struct ProvisioningSession {
  uint32_t schemaVersion;
  char deviceId[kMaxText], organizationId[kMaxText], templateVersionId[kMaxText];
  char linkUrl[kMaxText], secret[kMaxText];
  uint64_t serverUtcMs;
  ProvisioningSession() : schemaVersion(0), serverUtcMs(0) { deviceId[0] = organizationId[0] = templateVersionId[0] = linkUrl[0] = secret[0] = 0; }
  bool valid() const { return schemaVersion == 1 && deviceId[0] && strncmp(linkUrl, "wss://", 6) == 0 && secret[0]; }
};

class LinkBootstrapClient {
 public:
  virtual ~LinkBootstrapClient() {}
  virtual bool beginBootstrap(const ProvisioningRequest& request) = 0;
  virtual ProvisioningPoll poll(ProvisioningSession& session) = 0;
};

class Provisioner {
 public:
  Provisioner(LinkBootstrapClient& client, Storage& storage, Clock& clock) : client_(client), storage_(storage), clock_(clock), status_(ProvisioningStatus::Idle), error_("") {}
  bool begin(const ProvisioningRequest& request) {
    if (!request.engineUrl[0] || !request.token[0] || !request.hardwareId[0]) return fail("invalid_provisioning_input");
#ifndef FLOVA_ALLOW_INSECURE_PROVISIONING
    if (strncmp(request.engineUrl, "https://", 8) != 0) return fail("https_required");
#endif
    status_ = ProvisioningStatus::Bootstrapping; error_ = ""; return client_.beginBootstrap(request) || fail("bootstrap_start_failed");
  }
  void run() {
    if (status_ != ProvisioningStatus::Bootstrapping) return;
    ProvisioningPoll result = client_.poll(pending_); if (result == ProvisioningPoll::Pending) return;
    if (result == ProvisioningPoll::Failed || !pending_.valid()) { fail("invalid_provisioning_response"); return; }
    status_ = ProvisioningStatus::Committing;
    if (!storage_.write("session.pending", &pending_, sizeof(pending_)) || !storage_.write("session", &pending_, sizeof(pending_))) { fail("session_storage_failed"); return; }
    storage_.remove("session.pending"); if (pending_.serverUtcMs) clock_.setUtc(pending_.serverUtcMs, 1000); status_ = ProvisioningStatus::Ready;
  }
  bool load(ProvisioningSession& session) { return storage_.read("session", &session, sizeof(session)) && session.valid(); }
  ProvisioningStatus status() const { return status_; }
  const char* error() const { return error_; }
 private:
  bool fail(const char* error) { status_ = ProvisioningStatus::Failed; error_ = error; return false; }
  LinkBootstrapClient& client_; Storage& storage_; Clock& clock_; ProvisioningStatus status_; const char* error_; ProvisioningSession pending_;
};
}  // namespace flova
