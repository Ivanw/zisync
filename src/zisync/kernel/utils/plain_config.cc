
#include <memory>
#include <cstring>
#include <stdlib.h>

#include "zisync/kernel/utils/plain_config.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/cipher.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/database/content_provider.h"

namespace zs {
using std::unique_ptr;

PlainConfig PlainConfig::singleton;

bool PlainConfig::HasInit() {
  MutexAuto mut(&singleton.mutex);
  return singleton.has_init;
}

void PlainConfig::Clear() {
MutexAuto mut(&singleton.mutex);
singleton.has_init = false;
singleton.dictionary.clear();
}
err_t PlainConfig::Initialize(const string &app_data, const vector<string> &token) {

  if (!OsDirExists(app_data)) {
    int ret = OsCreateDirectory(app_data, false);
    if (ret == -1) {
      ZSLOG_ERROR("Create home_dir(%s) fail : %s",
                  app_data.c_str(), OsGetLastErr());
      return ZISYNC_ERROR_GENERAL;
    }
  }

  std::string plain_db_path = app_data;
  if (OsPathAppend(&plain_db_path, "Database") != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Failed appending path %s onto %s"
        , "Database", singleton.database_dir_.c_str());
    return ZISYNC_ERROR_BAD_PATH;
  }

  if (!OsDirExists(plain_db_path)) {
    int ret = OsCreateDirectory(plain_db_path, false);
    if (ret == -1) {
      ZSLOG_ERROR("Create home_dir(%s) fail : %s",
                  plain_db_path.c_str(), OsGetLastErr());
      return ZISYNC_ERROR_GENERAL;
    }
  }

  {
    MutexAuto mut(&singleton.mutex);
    singleton.has_init = true;
    singleton.database_dir_ = plain_db_path;
  }

  if (OsPathAppend(&plain_db_path, PlainConfig::plain_db_file()) != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Failed appending path %s onto %s"
        , PlainConfig::plain_db_file().c_str(), singleton.database_dir_.c_str());
    return ZISYNC_ERROR_BAD_PATH;
  }

  const char *projs[] = {
    TableMisc::COLUMN_KEY,
    TableMisc::COLUMN_VALUE,
  };

  unique_ptr<IContentProvider> plain_provider(new HistoryProvider());
  plain_provider->OnCreate(plain_db_path.c_str());

  std::map<std::string, std::string> tmp;
  unique_ptr<ICursor2> cursor(plain_provider->Query(TableMisc::URI
        , projs, ARRAY_SIZE(projs), NULL));
  while (cursor->MoveToNext()) {
    assert(cursor->GetString(0));
    assert(cursor->GetString(1));
    tmp[cursor->GetString(0)] = cursor->GetString(1);
  }
  cursor.reset(NULL);

  {
    MutexAuto mut(&singleton.mutex);
    singleton.dictionary.swap(tmp);
  }

  std::string mtoken;
  err_t rv = GetValueForKey("mtoken", &mtoken);

  if (rv != ZISYNC_SUCCESS || mtoken.length() == 0) {
    SetSqlcipherToken(token[0]);
  }
  return ZISYNC_SUCCESS;
}

std::string PlainConfig::plain_db_file() {
  return "ZiSync.Plain.db";
}

void PlainConfig::set_plain_db_file(const string &file) {assert(false);}

std::string PlainConfig::ciphered_db_file() {
  return "ZiSync.Secure.db";
}

void PlainConfig::set_ciphered_db_file(const string &file){assert(false);}

err_t PlainConfig::GetValueForKey(const std::string &key, std::string *val) {
  assert(val);
  {
    MutexAuto mut(&singleton.mutex);
    auto find = singleton.dictionary.find(key);
    if (find != singleton.dictionary.end()) {
      val->assign(find->second);
      return ZISYNC_SUCCESS;
    }
  }

  const char *projs[] = {
    TableMisc::COLUMN_VALUE,
  };

  std::string db_path = singleton.database_dir_;
  if (OsPathAppend(&db_path, PlainConfig::plain_db_file()) != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Failed appending path %s onto %s"
        , PlainConfig::plain_db_file().c_str(), singleton.database_dir_.c_str());
    return ZISYNC_ERROR_BAD_PATH;
  }

  unique_ptr<IContentProvider> plain_provider(new HistoryProvider());
  plain_provider->OnCreate(db_path.c_str());

  std::string where;
  StringFormat(&where, "%s = '%s'", TableMisc::COLUMN_KEY, key.c_str());
  unique_ptr<ICursor2> cursor;
  cursor.reset(plain_provider->Query(TableMisc::URI
        , projs, ARRAY_SIZE(projs), where.c_str()));
  if (!cursor->MoveToNext()) {
    return ZISYNC_ERROR_CONTENT;
  }

  assert(cursor->GetString(0));
  *val = cursor->GetString(0);
  cursor.reset(NULL);
  return ZISYNC_SUCCESS;
}

err_t PlainConfig::SetValueForKey(const std::string &key, const std::string &val) {
//  {
//    MutexAuto mut(&singleton.mutex);
//    auto find = singleton.dictionary.find(key);
//    if (find != singleton.dictionary.end() && find->second == val) {
//      return ZISYNC_SUCCESS;
//    }
//  }

  ContentValues cv(2);
  cv.Put(TableMisc::COLUMN_KEY, key.c_str());
  cv.Put(TableMisc::COLUMN_VALUE, val.c_str());

  std::string db_path = singleton.database_dir_;
  if (OsPathAppend(&db_path, PlainConfig::plain_db_file()) != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Failed appending path %s onto %s"
        , PlainConfig::plain_db_file().c_str(), singleton.database_dir_.c_str());
    return ZISYNC_ERROR_BAD_PATH;
  }

  unique_ptr<IContentProvider> plain_provider(new HistoryProvider());
  plain_provider->OnCreate(db_path.c_str());

  if (plain_provider->Insert(TableMisc::URI, &cv, AOC_REPLACE) != -1) {
    MutexAuto mutx(&singleton.mutex);
    singleton.dictionary[key] = val;
    return ZISYNC_SUCCESS;
  }else {
    return ZISYNC_ERROR_CONTENT;
  }
}

err_t PlainConfig::GetSqlcipherPassword(std::string *passphrase) {
  assert(passphrase);
  passphrase->assign("Zr^wWK5[h*!7J@)k]");

  std::string mtoken;
  err_t rv = GetValueForKey("mtoken", &mtoken);

  if (rv != ZISYNC_SUCCESS) {
    passphrase->clear();
    return ZISYNC_ERROR_CONTENT;
  }else {
    passphrase->append(mtoken);
    return ZISYNC_SUCCESS;
  }
}

err_t PlainConfig::SetSqlcipherToken(const std::string &passphrase) {
  return SetValueForKey("mtoken", passphrase);
}

}//namespace zs

