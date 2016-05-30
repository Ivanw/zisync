/* Copyright [2014] <zisync.com> */

#include <string>
#include <string.h>
#include <vector>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/utils/plain_config.h"
namespace zs {

using std::unique_ptr;
using std::shared_ptr;
using std::make_shared;
using std::vector;

// class ResolverRwLock {
//  public:
//   ResolverRwLock():layer(0) {}
//   ~ResolverRwLock() {}
//   
//   void AquireRdLock() {
//     if (layer == 0) {
//       rwlock_.AquireRdLock();
//     }
//     layer ++;
//   }
//   
//   void AquireWrLock() {
//     assert(layer == 0);
//     rwlock_.AquireWrLock();
//     layer ++;
//   }
// 
//   void ReleaseLock() {
//     layer --;
//     if (layer == 0) {
//       rwlock_.ReleaseLock();
//     }
//   }
// 
//  private:
//   RwLock rwlock_;
//   int layer;
// };
// 
// class ResolverRwLockRdAuto {
//  public:
//   explicit ResolverRwLockRdAuto(ResolverRwLock* rwlock)
//       : rwlock_(rwlock) {
//         rwlock_->AquireRdLock();
//       }
// 
//   ~ResolverRwLockRdAuto() {
//     rwlock_->ReleaseLock();
//   }
// 
//  private:
//   ResolverRwLockRdAuto(ResolverRwLockRdAuto&);
//   void operator=(ResolverRwLockRdAuto&);
// 
//   ResolverRwLock* rwlock_;
// };
// 
// class ResolverRwLockWrAuto {
//  public:
//   explicit ResolverRwLockWrAuto(ResolverRwLock* rwlock)
//       : rwlock_(rwlock) {
//         rwlock_->AquireWrLock();
//       }
// 
//   ~ResolverRwLockWrAuto() {
//     rwlock_->ReleaseLock();
//   }
// 
//  private:
//   ResolverRwLockWrAuto(ResolverRwLockWrAuto&);
//   void operator=(ResolverRwLockWrAuto&);
// 
//   ResolverRwLock* rwlock_;
// };
// 

//static int backupDb(
//    sqlite3 *pDb,               /* Database to back up */
//    const char *zFilename,      /* Name of file to back up to */
//    const char *key
//
//    ){
//  int rc;                     /* Function return code */
//  sqlite3 *pFile;             /* Database connection opened on zFilename */
//  sqlite3_backup *pBackup;    /* Backup handle used to copy data */
//
//  rc = sqlite3_open(zFilename, &pFile);
//  if( rc==SQLITE_OK  ){
//  
//    pBackup = sqlite3_backup_init(pFile, "main", pDb, "main");
//    if( pBackup  ){
//
//      do {
//        rc = sqlite3_backup_step(pBackup, 10);
//      } while( rc==SQLITE_OK || rc==SQLITE_BUSY || rc==SQLITE_LOCKED  );
//
//      if (rc != SQLITE_DONE) {
//        ZSLOG_ERROR("%s", "Backup db failed.");
//      }
//      (void)sqlite3_backup_finish(pBackup);
//
//    }
//
//  }
//
//  (void)sqlite3_close(pFile);
//  return rc;
//
//}
//
//static err_t EncryptPlaintextDatabase(const char *db_path, const char *key) {
//  int ret = 0;
//  sqlite3 *hsqlite_;
//  if (sqlite3_open(db_path, &hsqlite_) != 0) {
//    std::string errmsg;
//    StringFormat(&errmsg, "sqlite3_open(%s) failed : %s", db_path,
//        sqlite3_errmsg(hsqlite_));
//    ZSLOG_ERROR("%s", errmsg.c_str());
//    return ZISYNC_ERROR_SQLITE;
//  }
//  if (key) {
//    int keylen = strlen(key);
//    if (keylen && sqlite3_rekey(hsqlite_, (const void*)key, keylen) != 0) {
//      ZSLOG_ERROR("sqlite3_key failed");
//      return ZISYNC_ERROR_SQLITE;
//    }
//    do {
//      ret = sqlite3_close(hsqlite_);
//    } while (ret != SQLITE_OK);
//
//  }
//  return ZISYNC_SUCCESS;
//}
//
static RwLock providers_rwlock;
//static Mutex providers_mutex;
static string meta_data_dir;

  static err_t CreateEncryptedEmptyDb(const char *lpMetaDataDir
                                      , const char *key) {
    
    std::string secure_db_file_tmp = lpMetaDataDir;
    std::string secure_db_file = lpMetaDataDir;
    std::string buffer = lpMetaDataDir;
    bool secure_file_exist = false;
    bool old_file_exist = false;
    
    if (OsPathAppend(&secure_db_file_tmp, std::string("ZiSync.Secure.tmp.db")) != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Path buffer over follow: %s", secure_db_file_tmp.c_str());
      return ZISYNC_ERROR_BAD_PATH;
    }
    
    if (OsPathAppend(&secure_db_file, std::string("ZiSync.Secure.db")) != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Path buffer over follow: %s", secure_db_file.c_str());
      return ZISYNC_ERROR_BAD_PATH;
    }
    
    if (OsPathAppend(&buffer, std::string("ZiSync.db")) != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Path buffer over follow: %s", buffer.c_str());
      return ZISYNC_ERROR_BAD_PATH;
    }
    
    if (OsFileExists(buffer)) {
      old_file_exist = true;
    }
    
    if (OsFileExists(secure_db_file)) {
      secure_file_exist = true;
    }
    
    if (OsFileExists(secure_db_file_tmp)) {
      secure_file_exist = false;
      if (OsDeleteFile(secure_db_file_tmp.c_str(), false) != 0) {
        ZSLOG_ERROR("%s", "Cannot delte tmp secure db file.");
        return ZISYNC_ERROR_OS_IO;
      } 
      if(OsFileExists(secure_db_file)) {
        if (OsDeleteFile(secure_db_file.c_str(), false)) {
          ZSLOG_ERROR("%s", "Cannot delte secure db file.");
          return ZISYNC_ERROR_OS_IO;
        }
      }
    }
    
    err_t eno = ZISYNC_SUCCESS;
    
    if (old_file_exist && !secure_file_exist) {
      unique_ptr<IContentProvider> provider_tmp(new ContentProvider());
      eno = provider_tmp->OnCreate(secure_db_file_tmp.c_str(), key);
      if (eno == ZISYNC_SUCCESS) {
        unique_ptr<IContentProvider> provider_old(new ContentProvider());
        eno = provider_old->OnCreate(buffer.c_str(), NULL);
        if (eno == ZISYNC_SUCCESS) {
          if (((ContentProvider*)provider_tmp.get())->CopyFromDatabase(
                                                                       *(ContentProvider*)provider_old.get())) {
            ZSLOG_ERROR("%s", "Copy from old database failed.");
          }
        }
        provider_old.reset(NULL);
        provider_tmp.reset(NULL);
      }
      OsRename(secure_db_file_tmp.c_str(), secure_db_file.c_str(), true);
    }
    
    return ZISYNC_SUCCESS;
    
  }
  
err_t StartupContentFramework(const char* lpMetaDataDir, const vector<string> *mtokens) {
  err_t eno = ZISYNC_SUCCESS;

  meta_data_dir = lpMetaDataDir;
  sqlite3_temp_directory = &(*meta_data_dir.begin());

  IContentResolver *resolver = GetContentResolver();

  if (OsCreateDirectory(meta_data_dir, true) != zs::ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Failed to init application directory: %s", meta_data_dir.c_str());
    return ZISYNC_ERROR_OS_IO;
  }


  std::string secure_db_file = lpMetaDataDir;
  if (OsPathAppend(&secure_db_file, PlainConfig::ciphered_db_file()) != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Path buffer over follow: %s", secure_db_file.c_str());
    return ZISYNC_ERROR_BAD_PATH;
  }

    std::string passphrase;
    if (PlainConfig::GetSqlcipherPassword(&passphrase) != ZISYNC_SUCCESS) {
      if (PlainConfig::SetSqlcipherToken(mtokens->at(0).c_str()) != ZISYNC_SUCCESS) {
        assert(false);
        ZSLOG_ERROR("Failed to set sqlcipher password.");
        return ZISYNC_ERROR_CONTENT;
      }else {
        if (PlainConfig::GetSqlcipherPassword(&passphrase) != ZISYNC_SUCCESS) {
          assert(false);
          ZSLOG_ERROR("Failed to get sqlcipher password.");
          return ZISYNC_ERROR_CONTENT;
        }
      }
    }
  
  CreateEncryptedEmptyDb(lpMetaDataDir, passphrase.c_str());
  
  IContentProvider *provider = new ContentProvider();
  eno = provider->OnCreate(secure_db_file.c_str(), passphrase.c_str());
  if (eno != ZISYNC_SUCCESS) {
    delete provider;
    return eno;
  }
  if (!resolver->AddProvider(provider)) {
    delete provider;
    return ZISYNC_ERROR_CONTENT;
  }

  std::string buffer_plain = lpMetaDataDir;
  if (OsPathAppend(&buffer_plain, PlainConfig::plain_db_file()) != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Path buffer over follow: %s", buffer_plain.c_str());
    return ZISYNC_ERROR_BAD_PATH;
  }

  IContentProvider *history_provider = new HistoryProvider();
  eno = history_provider->OnCreate(buffer_plain.c_str());
  if (eno != ZISYNC_SUCCESS) {
    delete history_provider;
    return eno;
  }

  if (!resolver->AddProvider(history_provider)) {
    delete history_provider;
    return ZISYNC_ERROR_CONTENT;
  }

  const char *projections[] = {
    TableTree::COLUMN_UUID
  };
  ICursor2 *cursor = resolver->Query(
      TableTree::URI, projections, ARRAY_SIZE(projections), 
      "%s = %d", TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL);
  std::string db_file;
  while (cursor->MoveToNext()) {
    const char *tree_uuid = cursor->GetString(0);
    IContentProvider *provider = new TreeProvider(tree_uuid);
    db_file = lpMetaDataDir;
    eno = provider->OnCreate(lpMetaDataDir); 
    if (eno != ZISYNC_SUCCESS) {
      delete provider;
      break;
    }
    if (!resolver->AddProvider(provider)) {
      delete provider;
      eno = ZISYNC_ERROR_CONTENT;
      break;
    }
  }
  delete cursor;

  if (eno != ZISYNC_SUCCESS) {
    ShutdownContentFramework();
  }
  
  return ZISYNC_SUCCESS;
}

err_t ShutdownContentFramework() {
  IContentResolver *resolver = GetContentResolver();

  return resolver->Cleanup();
}

class CursorProviderDeallocator : public ICursor2Deallocator {
 public:
  CursorProviderDeallocator(const shared_ptr<IContentProvider> &provider):
      provider_(provider) {}
  virtual ~CursorProviderDeallocator() {}
  
  void set_provider(const shared_ptr<IContentProvider> &provider) {
    provider_ = provider;
  }
 private:
  shared_ptr<IContentProvider> provider_;
};

ContentResolver ContentResolver::s_hInstance;

IContentResolver* GetContentResolver() {
  return &ContentResolver::s_hInstance;
}

ContentResolver::ContentResolver() {
}


ContentResolver::~ContentResolver(void) {
}

err_t ContentResolver::Cleanup() {
  RwLockWrAuto mutex(&providers_rwlock);
  providers_map.clear();
  return ZISYNC_SUCCESS;
}

err_t ContentResolver::Clear(const Uri& uri) {
  shared_ptr<IContentProvider> pProvider;
  {
    RwLockWrAuto mutex(&providers_rwlock);
    auto find = providers_map.find(uri.GetAuthority());
    if (find != providers_map.end()) {
      pProvider = find->second;
      providers_map.erase(find);
    }
  }

  if (pProvider) {
    pProvider->Clear(uri);
    RwLockWrAuto mutex(&providers_rwlock);
    shared_ptr<IContentProvider> &find = providers_map[uri.GetAuthority()];
    if (!find) {
      find = pProvider;
    }
    return ZISYNC_SUCCESS;
  }
  return ZISYNC_SUCCESS;
}

ICursor2* ContentResolver::Query(
    const Uri& uri, const char* projection[],
    int projection_count, const char* selection, ...) {
  va_list ap;
  va_start(ap, selection);
  ICursor2* pCursor = vQuery(
      uri, projection, projection_count, selection, ap);
  va_end(ap);

  return pCursor;
}

class NullCursor : public ICursor2 {
 public:
  NullCursor() {}
  virtual void Cleanup() {}
  virtual bool MoveToNext() {
    return false;
  }
  virtual bool IsAfterLast() {
    return true;
  }

  virtual bool GetBool(int32_t iCol) {
	  assert(false);
	  return false;
  }

  virtual int32_t GetInt32(int32_t iCol) {
    assert(false);
    return 0;
  }
  virtual int64_t GetInt64(int32_t iCol) {
    assert(false);
    return 0;
  }
  virtual const char*  GetString(int32_t iCol) {
    assert(false);
    return NULL;
  }
  virtual const void*  GetBlobBase(int32_t iCol) {
    assert(false);
    return NULL;
  }
  virtual int32_t GetBlobSize(int32_t iCol) {
    assert(false);
    return 0;
  }
};



ICursor2* ContentResolver::vQuery(
    const Uri& uri, const char* projection[],
    int projection_count, const char* selection, va_list ap) {
  std::string where;
  if (selection != NULL) {
    StringFormatV(&where, selection, ap);
  }

  const shared_ptr<IContentProvider> &pProvider = FindProvider(uri);
  if (pProvider) {
    ICursor2 *cursor =  pProvider->Query(
        uri, projection, projection_count,
        selection ? where.c_str() : NULL);
    assert(cursor != NULL);
    cursor->AddDealloctor(new CursorProviderDeallocator(pProvider));
    return cursor;
  }

  return new NullCursor();
}

int32_t ContentResolver::Insert(
    const Uri& uri, ContentValues* values, OpOnConflict on_conflict) {
  const shared_ptr<IContentProvider> &pProvider = FindProvider(uri);

  if (pProvider) {
    return pProvider->Insert(uri, values, on_conflict);
  }

  return -1;
}

int32_t ContentResolver::Update(
    const Uri& uri, ContentValues* values, const char* selection, ...) {
  int32_t nCount;

  va_list ap;
  va_start(ap, selection);
  nCount = vUpdate(uri, values, selection, ap);
  va_end(ap);

  return nCount;
}

int32_t ContentResolver::vUpdate(
    const Uri& uri, ContentValues* values, const char* selection, va_list ap) {
  std::string where;

  if (selection != NULL) {
    StringFormatV(&where, selection, ap);
  }

  const shared_ptr<IContentProvider> &pProvider = FindProvider(uri);

  if (pProvider) {
    return pProvider->Update(
        uri, values, selection ? where.c_str() : NULL);
  }

  return -1;
}

int32_t ContentResolver::Delete(const Uri& uri, const char* selection, ...) {
  int32_t nCount;

  va_list ap;
  va_start(ap, selection);
  nCount = vDelete(uri, selection, ap);
  va_end(ap);

  return nCount;
}

int32_t ContentResolver::vDelete(
    const Uri& uri, const char* selection, va_list ap) {
  std::string where;

  if (selection != NULL) {
    StringFormatV(&where, selection, ap);
  }

  const shared_ptr<IContentProvider> &pProvider = FindProvider(uri);

  if (pProvider) {
    return pProvider->Delete(uri, selection ? where.c_str() : NULL);
  }

  return -1;
}

ICursor2* ContentResolver::sQuery(
    const Uri& uri, const char* projection[], int projection_count,
    Selection* selection, const char* sort_order ) {
  const shared_ptr<IContentProvider> &pProvider = FindProvider(uri);

  if (pProvider) {
    ICursor2 *cursor = pProvider->sQuery(
        uri, projection, projection_count, selection, sort_order);
    assert(cursor != NULL);
    cursor->AddDealloctor(new CursorProviderDeallocator(pProvider));
    return cursor;
  }

  return new NullCursor();
}

int32_t ContentResolver::sUpdate(
    const Uri& uri, ContentValues* values, Selection* selection) {
  const shared_ptr<IContentProvider> &pProvider = FindProvider(uri);

  if (pProvider) {
    return pProvider->sUpdate(uri, values, selection);
  }

  return -1;
}

int32_t ContentResolver::sDelete(const Uri& uri, Selection* selection) {
  const shared_ptr<IContentProvider> &pProvider = FindProvider(uri);

  if (pProvider) {
    return pProvider->sDelete(uri, selection);
  }

  return -1;
}

int32_t ContentResolver::BulkInsert(
    const Uri& uri,
    ContentValues* values[], int32_t values_count,
    OpOnConflict on_conflict) {
  const shared_ptr<IContentProvider> &pProvider = FindProvider(uri);

  if (pProvider) {
    return pProvider->BulkInsert(uri, values, values_count, on_conflict);
  }

  return -1;
}

int32_t ContentResolver::ApplyBatch(
    // const Uri& uri,
    const char* authority,
    OperationList* op_list) {
  const shared_ptr<IContentProvider> &pProvider = FindProvider(authority);

  if (pProvider) {
    return pProvider->ApplyBatch(authority, op_list);
  }

  return -1;
}

shared_ptr<IContentProvider> ContentResolver::FindProvider(const Uri& uri) {
  return FindProvider(uri.GetAuthority());
}

shared_ptr<IContentProvider> ContentResolver::FindProvider(const char* authority) {
  RwLockRdAuto mutex(&providers_rwlock);
  auto find = providers_map.find(authority);
  if (find == providers_map.end()) {
    return NULL;
  } else {
    return find->second;
  }
}

bool ContentResolver::RegisterContentObserver(
    const Uri& uri, bool notify_decendents, ContentObserver* content_observer) {
  assert(content_observer);

  shared_ptr<IContentProvider> pProvider = FindProvider(uri);

  if (pProvider) {
    return pProvider->RegisterContentObserver(
        uri, notify_decendents, content_observer);
  }

  return false;
}

bool ContentResolver::UnregisterContentObserver(
    const Uri& uri, ContentObserver* content_observer ) {
  assert(content_observer);
  const shared_ptr<IContentProvider> &pProvider = FindProvider(uri);

  if (pProvider) {
    return pProvider->UnregisterContentObserver(uri, content_observer);
  }

  return false;
}

bool ContentResolver::NotifyChange(
    const Uri& uri, ContentObserver* content_observer) {
  const shared_ptr<IContentProvider> &pProvider = FindProvider(uri);

  if (pProvider) {
    return pProvider->NotifyChange(uri, content_observer);
  }

  return false;
}

bool ContentResolver::AddProvider(IContentProvider *provider) {
  RwLockWrAuto mutex(&providers_rwlock);
  // MutexAuto mutex(&providers_mutex);
  shared_ptr<IContentProvider> &find =
      providers_map[provider->GetAuthority()];
  if (!find) {
    find.reset(provider);
  }
  return true;
}

bool ContentResolver::DelProvider(const char *authority, bool is_on_delete) {
  shared_ptr<IContentProvider> provider;
  {
    RwLockWrAuto mutex(&providers_rwlock);
    // MutexAuto mutex(&providers_mutex);
    auto find = providers_map.find(authority);
    if (find != providers_map.end()) {
      provider = find->second;
      if (is_on_delete) {
        provider->SetDeleteOnCleanup();
      }
      providers_map.erase(find);
    }
  }
    
  return true;
}

}  // namespace zs
