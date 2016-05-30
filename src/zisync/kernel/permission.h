#ifndef ZISYNC_KERNEL_PERMISSION_H_
#define ZISYNC_KERNEL_PERMISSION_H_

#include "zisync_kernel.h"
#include "zisync/kernel/platform/platform.h"

#include <map>
#include <vector>

#include "zisync/kernel/proto/verify.pb.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/libevent/libevent_base.h"

namespace zs {

extern const char *DefaultKeyCode;

typedef enum {
  METHOD_BIND,
  METHOD_UNBIND,
  METHOD_VERIFY,
  METHOD_ERROR,
}Method_t;

class Licences: public ContentObserver {
  friend Licences *GetLicences();
 public:
  Licences();
  ~Licences();
  virtual void *OnQueryChange();
  virtual void OnHandleChange(void *lpChanges);
  err_t Initialize();
  err_t CleanUp();

  std::string perm_key();
  std::string mac_address();
  std::string qrcode();
  std::string role();
  int64_t expired_time();
  int64_t created_time();
  int64_t last_contact_time();
  int64_t expired_offline_time();
  int32_t status();

  err_t SavePermKey(const std::string &key);
  err_t SaveMacAddress(const std::string &mac_address);
  err_t SaveExpiredTime(int64_t expired_time);
  err_t SaveCreatedTime(int64_t created_time);
  err_t SaveLastContactTime(int64_t last_contact_time);
  err_t SaveExpiredOfflineTime(int64_t expired_offline_time);
  err_t SaveRole(const std::string &role);
  err_t SaveStatus(int32_t status);
  err_t SaveQRCode(const std::string &qrcode);
  bool is_time_expired();
  err_t QueryLicencesInfo(struct LicencesInfo *licences);
  
  LT_t LicenseType();
  bool ShouldBindTrial();

 private:
  AtomicInt64 expired_time_, created_time_,
          last_contact_time_, expired_offline_time_;
  AtomicInt32 status_;

  std::string  perm_key_, mac_address_, role_, qrcode_;
  OsMutex mutex_;
  static Licences *s_licences_;
};

Licences *GetLicences();

class Permission : public ContentObserver {
  friend Permission *GetPermission();
 public:
  Permission();
  ~Permission();
  virtual void *OnQueryChange();
  virtual void OnHandleChange(void *lpChanges);

  bool Verify(UserPermission_t per, void *data = NULL);
  err_t QueryPermission(std::map<UserPermission_t, int32_t> *perms);
  err_t Reset(const std::string &content, const std::string &custom_data,
              Method_t method = METHOD_VERIFY,  evhttp_err_t err = EV_HTTP_OK);
  err_t Verify(const std::string &key);
  err_t VerifyRequest(Http::VerifyRequest *request);
  err_t Initialize(const std::vector<std::string> *mactoken);
  VerifyStatus_t VerifyStatus();
  err_t Bind(const std::string &key);
  err_t Unbind();
  void CheckExpired();
  
 private:
  // for database
  err_t Insert(const std::map<UserPermission_t, int32_t> &perms);
  err_t Update(const std::map<UserPermission_t, int32_t> &perms);
  err_t Remove(const std::map<UserPermission_t, int32_t> &perms);
  err_t OnHandleBindResponse(const std::string &content
                             , const std::string &custom_data);
  err_t OnHandleUnbindResponse(const std::string &content
                               , const std::string &custom_data);
  err_t OnHandleVerifyResponse(const std::string &content
                               , const std::string &custom_data);
  err_t OnHandleError(const std::string &content, evhttp_err_t err
                      , const std::string &custom_data);

  std::map<UserPermission_t, int32_t> perms_;
  bool is_bind_;
  VerifyStatus_t status_;
  OsMutex mutex_;
  AtomicInt32 bind_retrys_;
  AtomicInt32 unbind_retrys_;

  static Permission *s_instance_;
  
  
public:
  err_t UseDefaultPermissions();
};

Permission *GetPermission();

}  // namespace zs

#endif  // ZISYNC_KERNEL_PERMISSION_H_
