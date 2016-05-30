/* Copyright [2014] <zisync.com> */

#ifndef ZISYNC_KERNEL_DATABASE_CONTENT_RESOLVER_H_
#define ZISYNC_KERNEL_DATABASE_CONTENT_RESOLVER_H_

#include <map>
#include <string>
#include <memory>
#include "zisync/kernel/database/icontent.h"

namespace zs {

class OsMutex;

class ContentResolver : public IContentResolver {
 public:
  ContentResolver();
  virtual ~ContentResolver(void);

  virtual err_t Cleanup();
  /*  remove all data in the uri */
  virtual err_t Clear(const Uri& uri);

  virtual ICursor2* Query(
      const Uri& uri,
      const char* projection[],
      int projection_count,
      const char* selection, ...);

  virtual ICursor2* vQuery(
      const Uri& uri,
      const char* projection[],
      int projection_count,
      const char* selection, va_list ap);

  virtual ICursor2* sQuery(
      const Uri& uri,
      const char* projection[],   int projection_count,
      Selection* selection, const char* sort_order);

  virtual int32_t Insert(
      const Uri& uri,
      ContentValues* values,
      OpOnConflict on_conflict);

  virtual int32_t Update(
      const Uri& uri,
      ContentValues* values,
      const char* selection, ...);

  virtual int32_t vUpdate(
      const Uri& uri,
      ContentValues* values,
      const char* selection, va_list ap);

  virtual int32_t sUpdate(
      const Uri& uri,
      ContentValues* values,
      Selection* selection);

  virtual int32_t Delete(
      const Uri& uri,
      const char* selection, ...);

  virtual int32_t vDelete(
      const Uri& uri,
      const char* selection, va_list ap);

  virtual int32_t sDelete(
      const Uri& uri,
      Selection* selection);

  virtual int32_t BulkInsert(
      const Uri& uri,
      ContentValues* values[],
      int32_t values_count,
      OpOnConflict on_conflict);

  virtual int32_t ApplyBatch(
      // const Uri& uri,
      const char* authority,
      OperationList* op_list);

  virtual bool RegisterContentObserver(
      const Uri& uri,
      bool notify_decendents,
      ContentObserver* content_observer);

  virtual bool UnregisterContentObserver(
      const Uri& uri,
      ContentObserver* content_observer);

  virtual bool NotifyChange(
      const Uri& uri,
      ContentObserver* content_observer);
  
  virtual bool AddProvider(IContentProvider* provider);
  virtual bool DelProvider(const char* authority, bool is_on_delete);

 public:
  static ContentResolver s_hInstance;

 protected:
  std::shared_ptr<IContentProvider> FindProvider(const Uri& uri);
  std::shared_ptr<IContentProvider> FindProvider(const char* authority);
  std::map<std::string, std::shared_ptr<IContentProvider>> providers_map;
};

}  // namespace zs


#endif  // ZISYNC_KERNEL_DATABASE_CONTENT_RESOLVER_H_
