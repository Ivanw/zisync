#include <assert.h>
#include <algorithm>
#include <memory>
#include <vector>

#include "zisync/kernel/permission.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/libevent/report_data_server.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/base64.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/configure.h"

namespace zs {

const int default_permissions[USER_PERMISSION_NUM] = {
  -1, -1, -1, -1,
  -1, -1, -1, -1,
  -1, -1, -1, -1,
  -1, -1, -1, -1};

const int64_t DefaultPermissionLifeTime = 30 * 24 * 3600;
const char *DefaultKeyCode = "FR1FH-L2CQ3-NGC0T-DH4DV-9LRLA-TRIAL";
  
Licences *Licences::s_licences_ = NULL;
Permission *Permission::s_instance_ = NULL;

static inline void DelBarFromString(std::string *str) {
  for (auto it = str->begin(); it != str->end(); ) {
    if (*it == '-') {
      it = str->erase(it);
    } else {
      ++it;
    }
  }
}

static inline void DisableAllTreesSync() {
  return;
  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(1);
  cv.Put(TableTree::COLUMN_IS_ENABLED, false);
  resolver->Update(TableTree::URI, &cv, "%s = %d AND %s = %d AND %s = %d",
                   TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
                   TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
                   TableTree::COLUMN_IS_ENABLED, true);
}

static inline void EnableAllTreeSync() {
  return;
  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(1);
  cv.Put(TableTree::COLUMN_IS_ENABLED, true);
  resolver->Update(TableTree::URI, &cv, "%s = %d AND %s = %d AND %s = %d",
                   TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
                   TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
                   TableTree::COLUMN_IS_ENABLED, false);
}

Licences *GetLicences() {
  if (Licences::s_licences_ == NULL) {
    Licences::s_licences_ = new Licences;
  }

  return Licences::s_licences_;
}

Licences::Licences() : expired_time_(0), created_time_(0),
    last_contact_time_(0), expired_offline_time_(0), status_(LS_INVALID) {
      int ret = mutex_.Initialize();
      assert(ret == 0);
}

Licences::~Licences() {
  int ret = mutex_.CleanUp();
  assert(ret == 0);
}

void* Licences::OnQueryChange() {
  return NULL;
}

err_t Licences::Initialize() {
  {
    MutexAuto mutex_auto(&mutex_);
    GetContentResolver()->RegisterContentObserver(
        TableLicences::URI, false, this);
    OnHandleChange(NULL);
  }

  if (is_time_expired()) {
    DisableAllTreesSync();
  } else {
    EnableAllTreeSync();
  }


  return ZISYNC_SUCCESS;
}

static inline int64_t strtoint64(const char *str) {
  int64_t value = 0;
  int ret = sscanf(str, "%" PRId64, &value);
  assert(ret == 1);

  return value;
}

err_t Licences::CleanUp() {
  MutexAuto mutex_auto(&mutex_);
  OperationList op_list;
  {
    ContentOperation *co = op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
    ContentValues *cv = co->GetContentValues();
    cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_EXPIRED_TIME);
    cv->Put(TableLicences::COLUMN_VALUE, 0);
  }
  {
    ContentOperation *co = op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
    ContentValues *cv = co->GetContentValues();
    cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_EXPIRED_OFFLINE_TIME);
    cv->Put(TableLicences::COLUMN_VALUE, 0);
  }

  {
    ContentOperation *co = op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
    ContentValues *cv = co->GetContentValues();
    cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_CREATED_TIME);
    cv->Put(TableLicences::COLUMN_VALUE, 0);
  }
  {
    ContentOperation *co = op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
    ContentValues *cv = co->GetContentValues();
    cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_LAST_CONTACT_TIME);
    cv->Put(TableLicences::COLUMN_VALUE, 0);
  }
  {
    ContentOperation *co = op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
    ContentValues *cv = co->GetContentValues();
    cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_ROLE);
    cv->Put(TableLicences::COLUMN_VALUE, "");
  }
  {
    ContentOperation *co = op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
    ContentValues *cv = co->GetContentValues();
    cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_PERM_KEY);
    cv->Put(TableLicences::COLUMN_VALUE, "");
  }
  {
    ContentOperation *co = op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
    ContentValues *cv = co->GetContentValues();
    cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_STATUS);
    cv->Put(TableLicences::COLUMN_VALUE, LS_INVALID);
  }
  {
    ContentOperation *co = op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
    ContentValues *cv = co->GetContentValues();
    cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_QRCODE);
    cv->Put(TableLicences::COLUMN_VALUE, "");
  }

  int n = GetContentResolver()->ApplyBatch(
      ContentProvider::AUTHORITY, &op_list);
  if (n != op_list.GetCount()) {
    ZSLOG_ERROR("Remove licences fail.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

void Licences::OnHandleChange(void *lpChanges) {
  IContentResolver *resolver = GetContentResolver();

  const char *projections[] = {
    TableLicences::COLUMN_NAME,
    TableLicences::COLUMN_VALUE,
  };

  std::unique_ptr<ICursor2> cursor(resolver->Query(
          TableLicences::URI, projections, ARRAY_SIZE(projections), NULL));
  while (cursor->MoveToNext()) {
    const char *name = cursor->GetString(0);
    const char *value = cursor->GetString(1);
    if (strcmp(name, TableLicences::NAME_MAC_ADDRESS) == 0) {
      mac_address_ = value;
    } else if (strcmp(name, TableLicences::NAME_EXPIRED_TIME) == 0) {
      expired_time_.set_value(strtoint64(value));
    } else if (strcmp(name, TableLicences::NAME_EXPIRED_OFFLINE_TIME) == 0) {
      expired_offline_time_.set_value(strtoint64(value));
    } else if (strcmp(name, TableLicences::NAME_ROLE) == 0) {
      role_ = value;
    } else if (strcmp(name, TableLicences::NAME_PERM_KEY) == 0) {
      perm_key_ = value;
    } else if (strcmp(name, TableLicences::NAME_CREATED_TIME) == 0) {
      created_time_.set_value(strtoint64(value));
    } else if (strcmp(name, TableLicences::NAME_LAST_CONTACT_TIME) == 0) {
      last_contact_time_.set_value(strtoint64(value));
    } else if (strcmp(name, TableLicences::NAME_STATUS) == 0) {
      status_.set_value(static_cast<LS_t>(std::atoi(value)));
    } else if (strcmp(name, TableLicences::NAME_QRCODE) == 0) {
      qrcode_ = base64_decode(value);
    } else {
      assert(0);
    }
  }
  
  if (ShouldBindTrial()) {
    ReportDataServer::GetInstance()->DelayAndBind();
  }
}

std::string Licences::perm_key() {
  MutexAuto mutex_auto(&mutex_);
  return perm_key_;
}

std::string Licences::mac_address() {
  MutexAuto mutex_auto(&mutex_);
  return mac_address_;
}

std::string Licences::role() {
  MutexAuto mutex_auto(&mutex_);
  if (LicenseType() == LT_DEFAULT) {
    return "";
  }
  return role_;
}

std::string Licences::qrcode() {
  MutexAuto mutex_auto(&mutex_);
  return qrcode_;
}

int64_t Licences::expired_time() {
  return expired_time_.value();
}

int64_t Licences::created_time() {
  return created_time_.value();
}

int64_t Licences::last_contact_time() {
  return last_contact_time_.value();
}

int64_t Licences::expired_offline_time() {
  return expired_offline_time_.value();
}

int32_t Licences::status() {
  return status_.value();
}

err_t Licences::SavePermKey(const std::string &key) {
  MutexAuto mutex_auto(&mutex_);
  IContentResolver *resolver = GetContentResolver();
  
  string tmp = key;
  DelBarFromString(&tmp);

  ContentValues cv(2);
  cv.Put(TableLicences::COLUMN_NAME, TableLicences::NAME_PERM_KEY);
  cv.Put(TableLicences::COLUMN_VALUE, key);

  int num_affected_row = resolver->Insert(
      TableLicences::URI, &cv, AOC_REPLACE);
  if (num_affected_row < 0) {
    ZSLOG_ERROR("Inster perm key into provider fail. May be you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

err_t Licences::SaveMacAddress(const std::string &mac_address) {
  MutexAuto mutex_auto(&mutex_);
  IContentResolver *resolver = GetContentResolver();

  ContentValues cv(2);
  cv.Put(TableLicences::COLUMN_NAME, TableLicences::NAME_MAC_ADDRESS);
  cv.Put(TableLicences::COLUMN_VALUE, mac_address);

  int num_affected_row = resolver->Insert(
      TableLicences::URI, &cv, AOC_REPLACE);

  if (num_affected_row < 0) {
    ZSLOG_ERROR("Insert local mac address into provider fail. May be you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

err_t Licences::SaveExpiredTime(int64_t expired_time) {
  MutexAuto mutex_auto(&mutex_);
  IContentResolver *resolver = GetContentResolver();

  std::string time;
  StringFormat(&time, "%" PRId64, expired_time);
  ContentValues cv(2);
  cv.Put(TableLicences::COLUMN_NAME, TableLicences::NAME_EXPIRED_TIME);
  cv.Put(TableLicences::COLUMN_VALUE, time);

  int num_affected_row = resolver->Insert(
      TableLicences::URI, &cv, AOC_REPLACE);
  if (num_affected_row < 0) {
    ZSLOG_ERROR("Inster expired time into provider fail. May be you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

err_t Licences::SaveCreatedTime(int64_t created_time) {
  MutexAuto mutex_auto(&mutex_);
  IContentResolver *resolver = GetContentResolver();

  std::string time;
  StringFormat(&time, "%" PRId64, created_time);
  ContentValues cv(2);
  cv.Put(TableLicences::COLUMN_NAME, TableLicences::NAME_CREATED_TIME);
  cv.Put(TableLicences::COLUMN_VALUE, time);

  int num_affected_row = resolver->Insert(
      TableLicences::URI, &cv, AOC_REPLACE);
  if (num_affected_row < 0) {
    ZSLOG_ERROR("Inster create time into provider fail. May be you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

err_t Licences::SaveLastContactTime(int64_t last_contact_time) {
  MutexAuto mutex_auto(&mutex_);
  IContentResolver *resolver = GetContentResolver();

  std::string time;
  StringFormat(&time, "%" PRId64, last_contact_time);
  ContentValues cv(2);
  cv.Put(TableLicences::COLUMN_NAME, TableLicences::NAME_LAST_CONTACT_TIME);
  cv.Put(TableLicences::COLUMN_VALUE, time);

  int num_affected_row = resolver->Insert(
      TableLicences::URI, &cv, AOC_REPLACE);
  if (num_affected_row < 0) {
    ZSLOG_ERROR("Inster last contact time into provider fail. May be you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

err_t Licences::SaveExpiredOfflineTime(int64_t expired_offline_time) {
  MutexAuto mutex_auto(&mutex_);
  IContentResolver *resolver = GetContentResolver();

  std::string time;
  StringFormat(&time, "%" PRId64, expired_offline_time) ;
  ContentValues cv(2);
  cv.Put(TableLicences::COLUMN_NAME, TableLicences::NAME_EXPIRED_OFFLINE_TIME);
  cv.Put(TableLicences::COLUMN_VALUE, time);

  int num_affected_row = resolver->Insert(
      TableLicences::URI, &cv, AOC_REPLACE);
  if (num_affected_row < 0) {
    ZSLOG_ERROR("Inster last contact time into provider fail. May be you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

err_t Licences::SaveRole(const std::string &role) {
  MutexAuto mutex_auto(&mutex_);
  IContentResolver *resolver = GetContentResolver();

  ContentValues cv(2);
  cv.Put(TableLicences::COLUMN_NAME, TableLicences::NAME_ROLE);
  cv.Put(TableLicences::COLUMN_VALUE, role);

  int num_affected_row = resolver->Insert(
      TableLicences::URI, &cv, AOC_REPLACE);
  if (num_affected_row < 0) {
    ZSLOG_ERROR("Inster role into provider fail. May be you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

err_t Licences::SaveStatus(int32_t status) {
  MutexAuto mutex_auto(&mutex_);
  assert(status < LS_NUM);
  IContentResolver *resolver = GetContentResolver();

  ContentValues cv(2);
  cv.Put(TableLicences::COLUMN_NAME, TableLicences::NAME_STATUS);
  cv.Put(TableLicences::COLUMN_VALUE, status);
  int num_affected_row = resolver->Insert(
      TableLicences::URI, &cv, AOC_REPLACE);
  if (num_affected_row < 0) {
    ZSLOG_ERROR("Insert status into provider fail. May be you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

err_t Licences::SaveQRCode(const std::string &qrcode) {
  MutexAuto mutex_auto(&mutex_);
  IContentResolver *resolver = GetContentResolver();

  ContentValues cv(2);
  cv.Put(TableLicences::COLUMN_NAME, TableLicences::NAME_QRCODE);
  cv.Put(TableLicences::COLUMN_VALUE, qrcode);
  int num_affected_row = resolver->Insert(
      TableLicences::URI, &cv, AOC_REPLACE);
  if (num_affected_row < 0) {
    ZSLOG_ERROR("Insert qrcode into provider fail. May be you have "
                "not Initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

bool Licences::is_time_expired() {
  if (status() == LS_OK) {
    if (expired_time() == -1 || expired_time() > OsTimeInS()) {
      if (expired_offline_time() == -1 ||
          expired_offline_time() > OsTimeInS()) {
        return false;
      } else {
        SaveStatus(LS_EXPIRED_OFFLINE_TIME);
      }
    } else {
      SaveStatus(LS_EXPIRED_TIME);
    }
  }

  return true;
}

err_t Licences::QueryLicencesInfo(
    struct LicencesInfo *licences) {
  assert(licences != NULL);
  licences->role = role();
  licences->qrcode = qrcode();
  licences->last_contact_time = last_contact_time();
  licences->expired_time = expired_time();
  licences->expired_offline_time = expired_offline_time();
  licences->created_time = created_time();
  licences->status = static_cast<LS_t>(status());
#ifdef ZS_TEST
  if (LicenseType() == LT_DEFAULT) {
    licences->status = LS_OK;
  }
#endif
  if (expired_time_.value() == -1) {
    licences->left_time = -1;
  } else if (expired_time() > last_contact_time()) {
    licences->left_time = expired_time() - last_contact_time();
  } else {
    licences->left_time = 0;
  }
  licences->license_type = LicenseType();
  
#ifndef NDEBUG
  ZSLOG_INFO("role(%s), last_contact_time(%" PRId64 "), "
             "expired_time(%" PRId64 "), expired_offline_time(%" PRId64 "), "
             "created_time(%" PRId64 "), status(%d), left_time(%" PRId64 ")",
             licences->role.c_str(), licences->last_contact_time,
             licences->expired_time, licences->expired_offline_time,
             licences->created_time, licences->status,
             licences->left_time);
  ZSLOG_INFO("qrcode: %s", licences->qrcode.c_str());
#endif

  return ZISYNC_SUCCESS;
}
  
LT_t Licences::LicenseType() {
  std::string key = DefaultKeyCode;
  DelBarFromString(&key);
  
  std::string working = perm_key_;
  DelBarFromString(&working);
  
  if (perm_key_.size() == 0) {
    return LT_DEFAULT;
  }else if (working == key) {
    return LT_TRIAL;
  }else {
    return LT_PREMIUM;
  }
}

bool Licences::ShouldBindTrial() {
  if (LicenseType() == LT_DEFAULT) {
    return true;
  }else {
    if (status() != LS_OK) {
      return true;
    }
  }
  return false;
}

Permission::Permission() : is_bind_(false), status_(VS_OK),
    bind_retrys_(0), unbind_retrys_(0) {
  int ret = mutex_.Initialize();
  assert(ret == 0);
}

Permission::~Permission() {
  int ret = mutex_.CleanUp();
  assert(ret == 0);

  GetContentResolver()->UnregisterContentObserver(
      TablePermission::URI, this);
  s_instance_ = NULL;
}

static inline bool IsDataLimit(UserPermission_t perm) {
  if (perm == USER_PERMISSION_CNT_CREATE_TREE ||
      perm == USER_PERMISSION_CNT_CREATE_SHARE ||
      perm == USER_PERMISSION_CNT_BROWSE_REMOTE ||
      perm == USER_PERMISSION_CNT_STATICIP ||
      perm == USER_PERMISSION_CNT_BACKUP) {
      return true;
  }

  return false;
}

bool Permission::Verify(UserPermission_t perm, void *data) {
  return true;
  MutexAuto auto_mutex(&mutex_);
  auto it = perms_.find(perm);
  if (it != perms_.end()) {
    if (IsDataLimit(perm)) {
      if (it->second == -1 ||
          *reinterpret_cast<int32_t*>(data) < it->second) {
        status_ = VS_OK;
        return true;
      }
    } else {
      status_ = VS_OK;
      return true;
    }
  }

  status_ = VS_PERMISSION_DENY;
  return false;
}

err_t Permission::QueryPermission(
    std::map<UserPermission_t, int32_t> *perms) {
  assert(perms != NULL);
  MutexAuto auto_mutex(&mutex_);
  if (perms->empty()) {
    *perms= perms_;
  } else {
    for (auto it = perms->begin(); it != perms->end(); ) {
      if (perms_.find(it->first) != perms_.end()) {
        it->second = perms_[it->first];
        it++;
      } else {
        it = perms->erase(it);
      }
    }
  }

  return ZISYNC_SUCCESS;
}

Permission *GetPermission() {
  if (Permission::s_instance_ == NULL) {
    Permission::s_instance_ = new Permission;
  }

  return Permission::s_instance_;
}

static inline UserPermission_t ProtoCodeToUserCode(Http::PrivilegeCode code) {
  switch (code) {
    case Http::CreateFolder:
      return USER_PERMISSION_CNT_CREATE_TREE;
    case Http::ShareSwitch:
      return USER_PERMISSION_CNT_CREATE_SHARE;
    case Http::ShareReadWrite:
      return USER_PERMISSION_FUNC_SHARE_READWRITE;
    case Http::ShareRead:
      return USER_PERMISSION_FUNC_SHARE_READ;
    case Http::ShareWrite:
      return USER_PERMISSION_FUNC_SHARE_WRITE;
    case Http::DeviceSwitch:
      return USER_PERMISSION_FUNC_DEVICE_SWITCH;
    case Http::DeviceEdit:
      return USER_PERMISSION_CNT_STATICIP;
    case Http::OnlineSwitch:
      return USER_PERMISSION_CNT_BROWSE_REMOTE;
    case Http::OnlineOpen:
      return USER_PERMISSION_FUNC_REMOTE_OPEN;
    case Http::OnlineDownload:
      return USER_PERMISSION_FUNC_REMOTE_DOWNLOAD;
    case Http::OnlineUpload:
      return USER_PERMISSION_FUNC_REMOTE_UPLOAD;
    case Http::TransferSwitch:
      return USER_PERMISSION_FUNC_TRANSFER_LIST;
    case Http::HistorySwitch:
      return USER_PERMISSION_FUNC_HISTROY;
    case Http::ChangeSharePermission:
      return USER_PERMISSION_FUNC_EDIT_SHARE_PERMISSION;
    case Http::RemoveShareDevice:
      return USER_PERMISSION_FUNC_REMOVE_SHARE_DEVICE;
    case Http::CreateBackup:
      return USER_PERMISSION_CNT_BACKUP;

    default:
      assert(0);
  }
}

  err_t Permission::OnHandleBindResponse(const std::string &content, const std::string &customed_data) {
    
  int code = 0;
  std::string qrcode;
  size_t pos = content.find('|');
  if (pos == std::string::npos) {
    code = std::atoi(content.c_str());
    if (code == Http::Ok) {  // content is string
      ZSLOG_ERROR("Bind fail: %s", content.c_str());
      status_ = VS_UNKNOW_ERROR;
      return ZISYNC_ERROR_CONTENT;
    }
  } else {
    code = std::atoi(content.substr(0, pos).c_str());
    if (code != Http::Ok || content.size() <= pos) {  // right format
      ZSLOG_ERROR("Bind fail: %s", content.c_str());
      status_ = VS_UNKNOW_ERROR;
      return ZISYNC_ERROR_CONTENT;
    }
    qrcode = content.substr(pos + 1);
  }
    
  is_bind_ = false;
    
  switch (code) {
    case Http::InvaildKeyCode:
      status_ = VS_INVALID_KEY_CODE;
      break;
    case Http::LimitedKeyBind:
      status_ = VS_LIMITED_KEY_BIND;
      break;
    case Http::Ok:
      {
        err_t zisync_ret = GetLicences()->SavePermKey(customed_data);
        if (zisync_ret != ZISYNC_SUCCESS) {
          status_ = VS_UNKNOW_ERROR;
          return zisync_ret;
        }

        zisync_ret = GetLicences()->SaveQRCode(qrcode);
        if (zisync_ret != ZISYNC_SUCCESS) {
          status_ = VS_UNKNOW_ERROR;
          return zisync_ret;
        }
        ReportDataServer::GetInstance()->Verify(
            GetLicences()->mac_address(),
            GetLicences()->perm_key());
      }
      break;

    default:
      assert(0);
      return ZISYNC_ERROR_INVALID_KEY_CODE;
  }

  return ZISYNC_SUCCESS;
}

err_t Permission::OnHandleUnbindResponse(const std::string &content
                                         , const std::string &custom_data) {
  unbind_retrys_.set_value(0);
  int code = std::atoi(content.c_str());
  Remove(perms_);
  GetLicences()->CleanUp();
  DisableAllTreesSync();
  
  UseDefaultPermissions();
  switch (code) {
    case Http::InvalidDevice:
      status_ = VS_DEVICE_NOT_EXISTS;
      break;
    case Http::InvaildKeyCode:
      status_ = VS_INVALID_KEY_CODE;
      break;
    case Http::Ok:
      status_ = VS_OK;
      break;
    default:
      assert(0);
      return ZISYNC_ERROR_INVALID_KEY_CODE;
  }

  return ZISYNC_SUCCESS;
}

err_t Permission::OnHandleVerifyResponse(const std::string &content
                                         , const std::string &custom_data) {
  Http::VerifyResponse response;
  if (response.ParseFromString(content) == false) {
    ZSLOG_ERROR("Parse verify response from content(%s) fail.",
                content.c_str());
    status_ = VS_UNKNOW_ERROR;
    return ZISYNC_ERROR_CONTENT;
  }

#ifndef NDEBUG
  ZSLOG_INFO("errorcode(%d), role(%s), expiredtime(%" PRId64 ")",
             response.errorcode(), response.role().c_str(),
             response.expiredtime());
  ZSLOG_INFO("permissions:");
  for (int i = 0; i < response.permissions_size(); i++) {
    ZSLOG_INFO("%d: privilege(%d), constraint(%s)",
               i, response.permissions(i).privilege(),
               response.permissions(i).constraint().c_str());
  }
#endif

  if (is_bind_) {
    is_bind_ = false;
    bind_retrys_.set_value(0);
    string key = custom_data;
    DelBarFromString(&key);
    if (key != response.keycode()) {
      status_ = VS_UNKNOW_ERROR;
      return ZISYNC_ERROR_GENERAL;
    }
  } else {
    string key = GetLicences()->perm_key();
    DelBarFromString(&key);
    if (key != response.keycode()) {
      status_ = VS_UNKNOW_ERROR;
      return ZISYNC_ERROR_GENERAL;
    }
  }

  switch (response.errorcode()) {
    case Http::Ok:
      {
        std::map<UserPermission_t, int32_t> new_perms;
        std::map<UserPermission_t, int32_t> modify_perms;
        std::map<UserPermission_t, int32_t> temp_perms = perms_;

        for (int i = 0; i < response.permissions_size(); i++) {
          UserPermission_t code =
              ProtoCodeToUserCode(response.permissions(i).privilege());
          int32_t data = std::atoi(response.permissions(i).constraint().c_str());

          auto it = temp_perms.find(code);
          if (it == temp_perms.end()) {
            new_perms[code] = data;
          } else {
            if (it->second != data) {
              modify_perms[code] = data;
            }
            temp_perms.erase(it);
          }
        }

        // update database
        OperationList op_list;
        for (auto it = new_perms.begin(); it != new_perms.end(); it++) {
          ContentOperation *co =
              op_list.NewInsert(TablePermission::URI, AOC_REPLACE);
          ContentValues *cv = co->GetContentValues();
          cv->Put(TablePermission::COLUMN_KEY, it->first);
          cv->Put(TablePermission::COLUMN_VALUE, it->second);
        }

        for (auto it = modify_perms.begin(); it != modify_perms.end(); it++) {
          ContentOperation *co = op_list.NewUpdate(TablePermission::URI,
                                                   "%s = %d",
                                                   TablePermission::COLUMN_KEY,
                                                   it->first);
          ContentValues *cv = co->GetContentValues();
          cv->Put(TablePermission::COLUMN_VALUE, it->second);
        }

        for (auto it = temp_perms.begin(); it != temp_perms.end(); it++) {
          op_list.NewDelete(TablePermission::URI, "%s = %d",
                            TablePermission::COLUMN_KEY, it->first);
        }

        if (response.has_expiredtime() &&
            response.expiredtime() != GetLicences()->expired_time()) {
          ContentOperation *co =
              op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
          ContentValues *cv = co->GetContentValues();
          cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_EXPIRED_TIME);
          cv->Put(TableLicences::COLUMN_VALUE, response.expiredtime());
        }

        if (response.has_createdtime() &&
            response.createdtime() != GetLicences()->created_time()) {
          ContentOperation *co =
              op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
          ContentValues *cv = co->GetContentValues();
          cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_CREATED_TIME);
          cv->Put(TableLicences::COLUMN_VALUE, response.createdtime());
        }

        if (response.has_lastcontacttime() &&
            response.lastcontacttime() != GetLicences()->last_contact_time()) {
          ContentOperation *co = 
              op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
            ContentValues *cv = co->GetContentValues();
            cv->Put(TableLicences::COLUMN_NAME,
                    TableLicences::NAME_LAST_CONTACT_TIME);
            cv->Put(TableLicences::COLUMN_VALUE, response.lastcontacttime());
        }

        if (response.has_expiredofflinetime() &&
            response.expiredofflinetime() != GetLicences()->expired_offline_time()) {
            ContentOperation *co =
                op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
            ContentValues *cv = co->GetContentValues();
            cv->Put(TableLicences::COLUMN_NAME,
                    TableLicences::NAME_EXPIRED_OFFLINE_TIME);
            cv->Put(TableLicences::COLUMN_VALUE, response.expiredofflinetime());
        }

        if (response.has_role() && response.role() != GetLicences()->role()) {
          ContentOperation *co =
              op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
          ContentValues *cv = co->GetContentValues();
          cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_ROLE);
          cv->Put(TableLicences::COLUMN_VALUE, response.role());
        }
        if (GetLicences()->status() != LS_OK) {
          ContentOperation *co =
              op_list.NewInsert(TableLicences::URI, AOC_REPLACE);
          ContentValues *cv = co->GetContentValues();
          cv->Put(TableLicences::COLUMN_NAME, TableLicences::NAME_STATUS);
          cv->Put(TableLicences::COLUMN_VALUE, LS_OK);
        }

        int n = GetContentResolver()->ApplyBatch(
            ContentProvider::AUTHORITY, &op_list);
        if (n != op_list.GetCount()) {
          ZSLOG_ERROR("Write permissions in database fail.");
          status_ = VS_UNKNOW_ERROR;
          return ZISYNC_ERROR_CONTENT;
        }
        status_ = VS_OK;
        EnableAllTreeSync();
      }
      break;
    case Http::TimeExpired:
      ZSLOG_INFO("The key has expired time.");
      status_ = VS_KEY_EXPIRED;
      Remove(perms_);
      GetLicences()->SaveStatus(LS_EXPIRED_TIME);
      DisableAllTreesSync();
      break;
    case Http::InvaildKeyCode:
      ZSLOG_INFO("Send invaild key code to server.");
      status_ = VS_INVALID_KEY_CODE;
      //Remove(perms_);
      GetLicences()->SaveStatus(LS_INVALID);
      DisableAllTreesSync();
      break;
    case Http::LimitedKeyBind:
      ZSLOG_INFO("Limited key bind in server.");
      status_ = VS_LIMITED_KEY_BIND;
      //Remove(perms_);
      GetLicences()->SaveStatus(LS_INVALID);
      DisableAllTreesSync();
      break;
    case Http::NotBinded:
      ZSLOG_INFO("The key not binded.");
      status_ = VS_DEVICE_NOT_BIND;
      //Remove(perms_);
      GetLicences()->SaveStatus(LS_INVALID);
      DisableAllTreesSync();
      break;
    case Http::InvalidDevice:
      ZSLOG_INFO("The device is not exist.");
      status_ = VS_DEVICE_NOT_EXISTS;
      //Remove(perms_);
      GetLicences()->SaveStatus(LS_INVALID);
      DisableAllTreesSync();
      break;
    case Http::Other:
      ZSLOG_INFO("Unknow error.");
      status_ = VS_UNKNOW_ERROR;
      //Remove(perms_);
      GetLicences()->SaveStatus(LS_INVALID);
      DisableAllTreesSync();
      break;
    default:
      status_ = VS_UNKNOW_ERROR;
      ZSLOG_ERROR("Invalid error code in verify response.");
      assert(0);
      break;
  }

  return ZISYNC_SUCCESS;
}

err_t Permission::OnHandleError(const std::string &content, evhttp_err_t err
                                , const std::string &custom_data) {
  if (err == EV_HTTP_REQ_NULL) {
    if (content == "bind" && bind_retrys_.value()) {
      bind_retrys_.FetchAndSub(1);
      ReportDataServer::GetInstance()->Bind(
          GetLicences()->mac_address(), custom_data, custom_data);
      return ZISYNC_SUCCESS;
    } else if (content == "verify" && bind_retrys_.value()) {
      bind_retrys_.FetchAndSub(1);
      ReportDataServer::GetInstance()->Verify(
          GetLicences()->mac_address(), GetLicences()->perm_key());
      return ZISYNC_SUCCESS;
    } else if (content == "unbind" && unbind_retrys_.value()) {
      unbind_retrys_.FetchAndSub(1);
      ReportDataServer::GetInstance()->Unbind(
          GetLicences()->mac_address(), GetLicences()->perm_key(), GetLicences()->perm_key());
      return ZISYNC_SUCCESS;
    }
  } else {
    if (content == "verify") {
      is_bind_ = false;
      bind_retrys_.set_value(0);
    } else if (content == "unbind") {
      unbind_retrys_.set_value(0);
    }
  }

  status_ = VS_NETWORK_ERROR;

  return ZISYNC_SUCCESS;
}

err_t Permission::Reset(const std::string &content,
                        const std::string &custom_data,
                        Method_t method,
                        evhttp_err_t err) {
  MutexAuto auto_mutex(&mutex_);

  ZSLOG_INFO("Content: %s", content.c_str());
  switch (method) {
    case METHOD_BIND:
      return OnHandleBindResponse(content, custom_data);
    case METHOD_UNBIND:
      return OnHandleUnbindResponse(content, custom_data);
    case METHOD_VERIFY:
      return OnHandleVerifyResponse(content, custom_data);
    case METHOD_ERROR:
      return OnHandleError(content, err, custom_data);
    default:
      assert(0);
      return ZISYNC_ERROR_INVALID_METHOD;
  }

  return ZISYNC_SUCCESS;
}

err_t Permission::Insert(
    const std::map<UserPermission_t, int32_t> &perms) {
  OperationList op_list;
  for (auto it = perms.begin(); it != perms.end(); it++) {
    ContentOperation *co =
        op_list.NewInsert(TablePermission::URI, AOC_REPLACE);
    ContentValues *cv = co->GetContentValues();
    cv->Put(TablePermission::COLUMN_KEY, it->first);
    cv->Put(TablePermission::COLUMN_VALUE, it->second);
  }
  int n = GetContentResolver()->ApplyBatch(
      ContentProvider::AUTHORITY, &op_list);
  if (n != op_list.GetCount()) {
    ZSLOG_ERROR("Insert permissions fail.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

err_t Permission::Update(
    const std::map<UserPermission_t, int32_t> &perms) {
  OperationList op_list;
  for (auto it = perms.begin(); it != perms.end(); it++) {
    ContentOperation *co = op_list.NewUpdate(TablePermission::URI,
                                             "%s = %d",
                                             TablePermission::COLUMN_KEY,
                                             it->first);
    ContentValues *cv = co->GetContentValues();
    cv->Put(TablePermission::COLUMN_VALUE, it->second);
  }
  int n = GetContentResolver()->ApplyBatch(
      ContentProvider::AUTHORITY, &op_list);
  if (n != op_list.GetCount()) {
    ZSLOG_ERROR("Update permissions fail.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

err_t Permission::Remove(
    const std::map<UserPermission_t, int32_t> &perms) {
  OperationList op_list;
  for (auto it = perms.begin(); it != perms.end(); it++) {
    op_list.NewDelete(TablePermission::URI, "%s = %d",
                      TablePermission::COLUMN_KEY, it->first);
  }

  int n = GetContentResolver()->ApplyBatch(
      ContentProvider::AUTHORITY, &op_list);
  if (n != op_list.GetCount()) {
    ZSLOG_ERROR("Remove permissions fail.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

  //not used?
err_t Permission::Verify(const std::string &key) {
  assert(false);
  return ZISYNC_SUCCESS;
  MutexAuto auto_mutex(&mutex_);
  std::string verify_key = key;
  DelBarFromString(&verify_key);
  is_bind_ = true;
  status_ = VS_WAITING;
  ReportDataServer::GetInstance()->Verify(
      GetLicences()->mac_address(), GetLicences()->perm_key());
  
  return ZISYNC_SUCCESS;
}

err_t Permission::VerifyRequest(Http::VerifyRequest *request) {
  assert(false);
  return ZISYNC_SUCCESS;
  MutexAuto auto_mutex(&mutex_);
  if (is_bind_) {
    return ZISYNC_ERROR_GENERAL;
  } else {
    request->set_keycode(GetLicences()->perm_key());
  }
  request->set_mactoken(GetLicences()->mac_address());

  return ZISYNC_SUCCESS;
}

void Permission::CheckExpired() {
  MutexAuto auto_mutex(&mutex_);
  if (GetLicences()->is_time_expired()) {
    status_ = VS_KEY_EXPIRED;
    Remove(perms_);
    DisableAllTreesSync();
  }
}

err_t Permission::Initialize(const std::vector<std::string> *mactokens) {
  {
    MutexAuto mutex_auto(&mutex_);
    
    GetContentResolver()->RegisterContentObserver(
                                                  TablePermission::URI, false, this);
    
    assert(mactokens);
    if (GetLicences()->mac_address().empty()) {
      GetLicences()->SaveMacAddress(mactokens->at(0));
      ReportDataServer::GetInstance()->ReportMactoken(
                                                      GetLicences()->mac_address());
    }
    
    bool find_mactoken_res = false;
    for(auto it = mactokens->begin(); it != mactokens->end(); ++it) {
      if (*it == GetLicences()->mac_address()) {
        find_mactoken_res = true;
        break;
      }
    }
    if (!find_mactoken_res) {
      ZSLOG_ERROR("Mac token is not same. Perhaps you have changed mac address.");
      return ZISYNC_ERROR_MACTOKEN_MISMATCH;
    }
    
    OnHandleChange(NULL);
  }
  
  //if (!GetLicences()->perm_key().empty()) {
  //  ReportDataServer::GetInstance()->Verify(
  //      GetLicences()->mac_address(), GetLicences()->perm_key());
  //} else {
  //  Bind(DefaultKeyCode);
  //}

  return ZISYNC_SUCCESS;
}

void *Permission::OnQueryChange() {
  return NULL;
}

void Permission::OnHandleChange(void *lpChanges) {
  IContentResolver *resolver = GetContentResolver();
  {
    const char *projections[] = {
      TablePermission::COLUMN_KEY,
      TablePermission::COLUMN_VALUE,
    };

    std::unique_ptr<ICursor2> cursor(resolver->Query(
            TablePermission::URI, projections,
            ARRAY_SIZE(projections),
            NULL));
    perms_.clear();
    while (cursor->MoveToNext()) {
      UserPermission_t code =
          static_cast<UserPermission_t>(cursor->GetInt32(0));
      int32_t data = cursor->GetInt32(1);
      perms_.insert(std::pair<UserPermission_t, int32_t>(code, data));
    }
  }
  
}

VerifyStatus_t Permission::VerifyStatus() {
  MutexAuto mutex_auto(&mutex_);

  return status_;
}

err_t Permission::Bind(const std::string &key) {
  MutexAuto mutex_auto(&mutex_);
  std::string verify_key = key;
  DelBarFromString(&verify_key);
  is_bind_ = true;
  bind_retrys_.set_value(1);
  status_ = VS_WAITING;
  ReportDataServer::GetInstance()->Bind(
      GetLicences()->mac_address(), key, key);

  return ZISYNC_SUCCESS;
}

err_t Permission::Unbind() {
  MutexAuto mutex_auto(&mutex_);
  unbind_retrys_.set_value(1);
  status_ = VS_WAITING;
  ReportDataServer::GetInstance()->Unbind(
      GetLicences()->mac_address(), GetLicences()->perm_key(), GetLicences()->perm_key());

  return ZISYNC_SUCCESS;
}
  
err_t Permission::UseDefaultPermissions() {
#ifdef ZS_TEST
  if (!Config::is_default_perms_enabled()) {
    return ZISYNC_SUCCESS;
  }
#endif
  err_t ret = ZISYNC_SUCCESS;
  ret = GetLicences()->SaveCreatedTime(OsTimeInS());
  if (ret != ZISYNC_SUCCESS) {
    return ret;
  }
  ret = GetLicences()->SaveExpiredTime(OsTimeInS() + DefaultPermissionLifeTime);
  if (ret != ZISYNC_SUCCESS) {
    return ret;
  }
  
  std::map<UserPermission_t, int32_t> default_perms_map;
  for(int i = 0; i < sizeof(default_permissions) / sizeof(default_permissions[0]); i++) {
    default_perms_map.insert(std::make_pair((UserPermission_t)i, default_permissions[i]));
  }
  
  Remove(default_perms_map);
  perms_ = default_perms_map;
  return Insert(default_perms_map);
}

}  // namespace zs
