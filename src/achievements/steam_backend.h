#pragma once
#include "achievements/service.h"
#include <functional>
#include <memory>
#include <vector>
namespace torchlight::achievements {
inline constexpr uint32_t kTorchlightAppId = 41500; // PC Steam restart call, 0x005F8792
struct SteamIdentity { uint32_t app=0; uint64_t account=0; bool logged_in=false, owns_app=false; };
struct SteamCallback {
  enum class Kind { Received, Stored } kind;
  uint32_t app=0; uint64_t account=0;
  bool success=false, invalid_parameter=false;
};
class SteamApi {
 public:
  virtual ~SteamApi() = default;
  virtual bool Initialize() = 0;
  virtual void Shutdown() = 0;
  virtual SteamIdentity Identity() = 0;
  virtual bool RequestStats() = 0;
  virtual std::vector<SteamCallback> Poll() = 0;
  virtual bool Achievement(std::string_view id, bool& unlocked) = 0;
  virtual bool Stat(std::string_view id, int32_t& value) = 0;
  virtual bool SetAchievement(std::string_view id) = 0;
  virtual bool SetStat(std::string_view id, int32_t value) = 0;
  virtual bool Store() = 0;
  virtual bool MutationsSupported() const = 0;
};
struct SteamOptions { bool enable_writes=false; uint64_t expected_account=0; };
// No gameplay producers live here. A batch is acknowledged only by a successful callback.
class SteamBackend {
 public:
  using Log = std::function<void(const std::string&)>;
  using Persist = std::function<bool()>;
  SteamBackend(Service&, SteamApi&, SteamOptions, Log, Persist);
  ~SteamBackend();
  void Tick(uint64_t milliseconds);
  std::string_view status() const { return status_; }
 private:
  bool IdentityValid();
  bool Read();
  void Failure(uint64_t now, std::string_view reason);
  Service& service_; SteamApi& api_; SteamOptions options_; Log log_; Persist persist_;
  State remote_, batch_;
  bool initialized_=false, requested_=false, ready_=false, waiting_=false, blocked_=false;
  uint64_t retry_at_=0, deadline_=0;
  std::string status_="offline", last_plan_, last_identity_;
};
std::unique_ptr<SteamApi> CreateNativeSteamApi(SteamOptions options = {});
} // namespace torchlight::achievements
