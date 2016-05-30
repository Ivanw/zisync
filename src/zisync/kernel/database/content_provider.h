/* Copyright [2014] <zisync.com> */

#ifndef ZISYNC_KERNEL_DATABASE_CONTENT_PROVIDER_H_
#define ZISYNC_KERNEL_DATABASE_CONTENT_PROVIDER_H_

#include <string>
#include <list>
#include <vector>
#include <unordered_map>
#include <memory>
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/xsqlite.h"

namespace zs {

using std::unordered_map;
using std::list;

// template <> inline
// size_t stdext::hash_value<std::string> (const std::string& s)
// {
//  return stdext::hash_value((const char*)s);
// }

class OsMutex;

#define  DATABASE_VERSION 13
class TreeProvider;
class ContentProvider : public IContentProvider {
 public:
  static const Uri URI;
  ContentProvider();
  virtual ~ContentProvider();

  virtual err_t OnCreate(const char* app_data, const char *key = NULL);
  virtual err_t Clear(const Uri& uri);
  virtual ICursor2* Query(
      const Uri& uri,
      const char* projection[],
      int projection_count,
      const char* where);

  virtual ICursor2* sQuery(
      const Uri& uri,
      const char* projection[], int projection_count,
      Selection* selection, const char* sort_order);

  virtual int32_t Insert(
      const Uri& uri,
      ContentValues* values,
      OpOnConflict on_conflict);

  virtual int32_t Update(
      const Uri& uri,
      ContentValues* values,
      const char* where);

  virtual int32_t sUpdate(
      const Uri& uri,
      ContentValues* values,
      Selection* selection);

  virtual int32_t Delete(
      const Uri& uri,
      const char* where);

  virtual int32_t sDelete(
      const Uri& uri,
      Selection* selection);

  virtual int32_t BulkInsert(
      const Uri& uri,
      ContentValues* values[],
      int32_t value_count,
      OpOnConflict on_conflict);

  virtual int32_t ApplyBatch(
      // const Uri& uri,
      const char* szAuthority,
      OperationList* op_list);

  virtual bool RegisterContentObserver(
      const Uri& uri,
      bool notify_for_decendents,
      ContentObserver* content_observer);

  virtual bool UnregisterContentObserver(
      const Uri& uri,
      ContentObserver* content_observer);

  virtual bool NotifyChange(
      const Uri& uri,
      ContentObserver* content_observer);

  virtual const char *GetAuthority() {
    return ContentProvider::AUTHORITY;
  }
  virtual void SetDeleteOnCleanup() {
    is_delete_on_cleanup_ = true;
  }
  
  static void GetTables(XSQLite*, std::vector<std::string>*);
  err_t CopyFromDatabase(ContentProvider&old);
  static const char AUTHORITY[];

 protected:
  static ContentProvider s_Instance;//actually not used
  virtual err_t OnCleanUp();
  virtual err_t OnDelete();

  virtual const char* GetTable(const Uri& uri);

  virtual bool OnCreateDatabase();
  virtual bool OnUpgradeDatabase(int pre_version);


  XSQLite hsqlite_;
  std::string app_data_;

  // for DeviceTTL
  // std::unordered_map<int32_t, int32_t> m_tabDeviceTTL;

  /* for content observer */
  OsMutex* mutex_;
  unordered_map<std::string, list<ContentObserver*>> map_tree_observer;
  unordered_map<std::string, list<ContentObserver*>> map_node_observer;

  bool is_delete_on_cleanup_;

 private:
  /*  if File table des not exist, create them */
  err_t CreateTableProviders();
};

/* URI format : TreeProvider::AUTHORITY/<tree_uuid>/file 
 * TreePrvider::AUTHORITY/<tree_uuid> is the Authority*/
class TreeProvider : public ContentProvider {
 public:
  explicit TreeProvider(const char* tree_uuid);
  virtual ~TreeProvider() {}

  virtual bool UriMatch(const Uri& uri);
  virtual bool AuthorityMatch(const char* authority);
  virtual const char* GetTable(const Uri& uri);

  virtual err_t OnCreate(const char* app_data, const char *key = NULL);
  err_t Create(const char *app_data, const char *tree_uuid);

  virtual bool OnCreateDatabase();
  virtual bool OnUpgradeDatabase(int pre_version);

  const std::string& tree_uuid() { return tree_uuid_; }
  virtual const char *GetAuthority() {
    return authority_.c_str();
  }

  static const char AUTHORITY[];

 private:
  const std::string tree_uuid_;
  const std::string authority_;
  
};

class HistoryProvider : public ContentProvider {
  public:
    explicit HistoryProvider();
    virtual ~HistoryProvider(){}

    virtual const char *GetAuthority() {
      return HistoryProvider::AUTHORITY;
    }
    virtual err_t OnCreate(const char *dirpath, const char *key = NULL){
      return ContentProvider::OnCreate(dirpath, key);
    }
    virtual bool OnCreateDatabase();
    virtual bool OnUpgradeDatabase(int pre_version);
    virtual err_t Clear(const Uri& uri);
    static const Uri URI;
    static const char AUTHORITY[];
  private:

};
//class HistoryProvider : public ContentProvider {
//  public:
//    explicit HistoryProvider();
//    virtual ~HistoryProvider(){}
//
//    virtual const char *GetAuthority() {
//      return HistoryProvider::AUTHORITY;
//    }
//    virtual err_t OnCreate(const char *dirpath, const char *key = NULL){
//      return ContentProvider::OnCreate(dirpath, key);
//    }
//    virtual bool OnCreateDatabase();
//    virtual bool OnUpgradeDatabase(int pre_version);
//
//    static const Uri URI;
//    static const char AUTHORITY[];
//  private:
//
//};

}  // namespace zs

#endif  // ZISYNC_KERNEL_DATABASE_CONTENT_PROVIDER_H_
