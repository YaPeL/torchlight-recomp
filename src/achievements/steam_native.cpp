#include "achievements/steam_backend.h"
#if defined(TORCHLIGHT_HAS_STEAMWORKS)
#include <steam/steam_api.h>
#endif
namespace torchlight::achievements {
namespace {
#if defined(TORCHLIGHT_HAS_STEAMWORKS)
// RequestCurrentStats is deprecated in current Steamworks; support SDKs retaining it,
// otherwise validate the preloaded client state through the same complete read path.
template<class T> bool Request(T* stats) {
  if constexpr (requires { stats->RequestCurrentStats(); }) return stats->RequestCurrentStats();
  else return true;
}
template<class T> constexpr bool HasRequest() {
  return requires(T* stats) { stats->RequestCurrentStats(); };
}
class NativeApi final : public SteamApi {
 public:
  explicit NativeApi(SteamOptions options) : options_(options) {}
  bool Initialize() override {
    if (!SteamAPI_Init()) return false;
    if (!SteamUser() || !SteamUserStats() || !SteamUtils() || !SteamApps()) {
      SteamAPI_Shutdown(); return false;
    }
    initialized_=true; return true;
  }
  void Shutdown() override { if (initialized_) SteamAPI_Shutdown(); initialized_=false; }
  SteamIdentity Identity() override {
    if (!initialized_) return {};
    return {SteamUtils()->GetAppID(), SteamUser()->GetSteamID().ConvertToUint64(),
            SteamUser()->BLoggedOn(), SteamApps()->BIsSubscribedApp(kTorchlightAppId)};
  }
  bool RequestStats() override {
    if (!initialized_) return false;
    const bool accepted=Request(SteamUserStats());
    if constexpr (!HasRequest<ISteamUserStats>()) {
      const auto identity=Identity();
      callbacks_.push_back({SteamCallback::Kind::Received,identity.app,identity.account,accepted,false});
    }
    return accepted;
  }
  std::vector<SteamCallback> Poll() override {
    SteamAPI_RunCallbacks(); auto result=std::move(callbacks_); callbacks_.clear(); return result;
  }
  bool Achievement(std::string_view id, bool& unlocked) override {
    return SteamUserStats()->GetAchievement(std::string(id).c_str(),&unlocked);
  }
  bool Stat(std::string_view id, int32_t& value) override {
    return SteamUserStats()->GetStat(std::string(id).c_str(),&value);
  }
  bool MutationsSupported() const override {
#if defined(TORCHLIGHT_STEAM_WRITES)
    return true;
#else
    return false;
#endif
  }
  bool SetAchievement(std::string_view id) override {
#if defined(TORCHLIGHT_STEAM_WRITES)
    if (!CanWrite()) return false;
    return SteamUserStats()->SetAchievement(std::string(id).c_str());
#else
    (void)id; return false;
#endif
  }
  bool SetStat(std::string_view id, int32_t value) override {
#if defined(TORCHLIGHT_STEAM_WRITES)
    if (!CanWrite()) return false;
    return SteamUserStats()->SetStat(std::string(id).c_str(),value);
#else
    (void)id; (void)value; return false;
#endif
  }
  bool Store() override {
#if defined(TORCHLIGHT_STEAM_WRITES)
    if (!CanWrite()) return false;
    store_account_=Identity().account;
    return SteamUserStats()->StoreStats();
#else
    return false;
#endif
  }
 private:
  bool CanWrite() {
    const auto i=Identity();
    return options_.enable_writes && options_.expected_account!=0 &&
        i.account==options_.expected_account && i.app==kTorchlightAppId && i.logged_in && i.owns_app;
  }
  SteamOptions options_;
  STEAM_CALLBACK(NativeApi, Received, UserStatsReceived_t, received_callback_);
  STEAM_CALLBACK(NativeApi, Stored, UserStatsStored_t, stored_callback_);
  bool initialized_=false;
  uint64_t store_account_=0;
  std::vector<SteamCallback> callbacks_;
};
void NativeApi::Received(UserStatsReceived_t* event) {
    callbacks_.push_back({SteamCallback::Kind::Received,CGameID(event->m_nGameID).AppID(),
        event->m_steamIDUser.ConvertToUint64(),event->m_eResult==k_EResultOK,
        event->m_eResult==k_EResultInvalidParam});
  }
void NativeApi::Stored(UserStatsStored_t* event) {
    callbacks_.push_back({SteamCallback::Kind::Stored,CGameID(event->m_nGameID).AppID(),
        store_account_,event->m_eResult==k_EResultOK,event->m_eResult==k_EResultInvalidParam});
  }

#else
class NativeApi final : public SteamApi {
 public:
  explicit NativeApi(SteamOptions) {}
  bool Initialize() override { return false; }
  void Shutdown() override {}
  SteamIdentity Identity() override { return {}; }
  bool RequestStats() override { return false; }
  std::vector<SteamCallback> Poll() override { return {}; }
  bool Achievement(std::string_view, bool&) override { return false; }
  bool Stat(std::string_view, int32_t&) override { return false; }
  bool SetAchievement(std::string_view) override { return false; }
  bool SetStat(std::string_view, int32_t) override { return false; }
  bool Store() override { return false; }
  bool MutationsSupported() const override { return false; }
};
#endif
}
std::unique_ptr<SteamApi> CreateNativeSteamApi(SteamOptions options) { return std::make_unique<NativeApi>(options); }
} // namespace torchlight::achievements
