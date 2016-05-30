/* Copyright [2014] <zisync.com> */
#ifndef ZISYNC_KERNEL_DATABASE_ICONTENT_H_
#define ZISYNC_KERNEL_DATABASE_ICONTENT_H_

#include <assert.h>

#include <string>
#include <vector>
#include <memory>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class IHandler;
class ContentOperation;

class Uri {
 public:
  explicit Uri(const char* szUri, ...);
  Uri(const char* schema, const char* authority, const char* path);
  virtual ~Uri() { /* virtual destructor */ }

  virtual const char* AsString() const;
  virtual const char* GetSchema() const;
  virtual const char* GetAuthority() const;
  virtual const char* GetPath() const;
  // virtual const char* GetPathSegments();
  virtual std::string GetLastPathSegment() const;

  friend bool operator==(
      const Uri& lhs,
      const Uri& rhs) throw() {
    return lhs.uri_ == rhs.uri_;
  }

 protected:
  std::string schema_;
  std::string authority_;
  std::string path_;

  std::string uri_;
};

class ICursor2Deallocator {
 public:
  virtual ~ICursor2Deallocator() {}
};

class ICursor2 {
 public:
  ICursor2() {}
  virtual ~ICursor2() {}

  // something that must do before deallocator
  virtual bool MoveToNext() = 0;
  virtual bool IsAfterLast() = 0;

  virtual bool GetBool(int32_t column) = 0;
  virtual int32_t GetInt32(int32_t column) = 0;
  virtual int64_t GetInt64(int32_t column) = 0;
  virtual const char*  GetString(int32_t column) = 0;
  virtual const void* GetBlobBase(int32_t column) = 0;
  virtual int32_t GetBlobSize(int32_t column) = 0;

  void AddDealloctor(ICursor2Deallocator *deallocator) {
    deallocators_.emplace_back(deallocator);
  }

 private:
  std::vector<std::unique_ptr<ICursor2Deallocator>> deallocators_;
};

class ContentObserver {
 public:
  ContentObserver();
  virtual ~ContentObserver();

  virtual void* OnQueryChange()                 = 0;
  virtual void  OnHandleChange(void* lpChanges) = 0;

  virtual IHandler* GetHandler() { return NULL; }
  void DispatchChange(bool bSelfChange);

 protected:
  bool      auto_delete_;
};

enum ValueType {
  AVT_INT32, AVT_INT64, AVT_TEXT, AVT_BLOB, AVT_LITERAL
};

class ContentValues {
 public:
  ContentValues();
  explicit ContentValues(int32_t count);
  virtual ~ContentValues();

  void Put(const char* key, bool value);
  void Put(const char* key, int32_t value);
  void Put(const char* key, int64_t value);
  void Put(const char* key, const char* value, bool duplicate = false);
  void Put(const char* key, const std::string& value, bool duplicate = false);
  void Put(const char* key,
           const void* value, int32_t nbytes, bool duplicate = false);
  void PutLiteral(const char* key,
                  const char* value, bool duplicate = false);

  int32_t GetCount();
  const char* GetKey(int32_t index);
  ValueType GetType(int32_t index);
  int32_t GetInt32(int32_t index);
  int64_t GetInt64(int32_t index);
  const char*  GetString(int32_t index);
  const void*  GetBlobBase(int32_t index);
  int32_t GetBlobSize(int32_t index);

 protected:
  struct ContentValue {
    ValueType type_;
    const char*   key_;
    bool      auto_delete_;
    union {
      int32_t i32_;
      int64_t i64_;
      const char* str_;
      struct {
        int32_t count_;
        const void* base_;
      };
    };
  };

  std::vector<struct ContentValue> elements_;
  std::vector<void*> duplicates_;
};

class Selection {
 public:
  Selection();

#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 2, 3)))
      Selection(const char* where, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  Selection(_Printf_format_string_ const char* where, ...);
# else
  Selection(__format_string const char* where, ...);
# endif /* FORMAT_STRING */
#else
  Selection(const char* where, ...);
#endif

  virtual ~Selection();

  void vFormatWhere(const char* where, va_list ap);

  void Add(int32_t value);
  void Add(int64_t value);
  void Add(const char* value, bool duplicate = false);
  void Add(void* value, int32_t nbytes, bool duplicate = false);
  // void AddLiteral(const char* key, const char* value);

  const char* GetWhere();
  int32_t   GetCount();
  ValueType GetType(int32_t index);
  int32_t   GetInt32(int32_t index);
  int64_t   GetInt64(int32_t index);
  const char* GetString(int32_t index);
  void*    GetBlobBase(int32_t index);
  int32_t   GetBlobSize(int32_t index);

 protected:
  // int32_t count_;
  struct Argument {
    ValueType type_;
    union {
      int32_t i32_;
      int64_t i64_;
      const char* value_;
      struct {
        int32_t count_;
        void*  base_;
      };
    };
  };

  std::string where_;
  std::vector<struct Argument> elements_;
  std::vector<void*> duplicates_;
};

enum OpType { AOT_INSERT, AOT_UPDATE, AOT_DELETE};
enum OpOnConflict { AOC_ABORT, AOC_IGNORE, AOC_REPLACE };

class OperationCondition {
 public:
  virtual ~OperationCondition() { /* */ }

  virtual bool Evaluate(ContentOperation *op) = 0;
};

class OperationPostProcess {
 public:
  virtual ~OperationPostProcess() {}
  virtual void Evaluate() = 0;
};

class ContentOperation {
 public:
  ContentOperation(const Uri& uri, OpType type)
      : uri_(uri), type_(type), 
      condition_(NULL), condition_need_delete_(false),
      post_process_(NULL), post_process_need_delete_(false) {
      }

  virtual ~ContentOperation() {
    if (condition_need_delete_ && condition_ != NULL) {
      delete condition_;
    }
  }

  virtual ContentValues* GetContentValues() = 0;
  virtual Selection*     GetSelection()     = 0;
  OperationCondition*    GetCondition() {
    return condition_;
  }
  void SetCondition(OperationCondition* condition, bool need_delete) {
    assert(condition_ == NULL);
    condition_ = condition;
    condition_need_delete_ = need_delete;
  }
  OperationPostProcess *GetPostProcess() {
    return post_process_;
  }
  void SetPostProcess(OperationPostProcess* post_process, bool need_delete) {
    assert(post_process_ == NULL);
    post_process_ = post_process;
    post_process_need_delete_ = need_delete;
  }

  virtual OpType          GetType() { return type_; }
  virtual Uri*           GetUri()  { return &uri_; }

 protected:
  Uri   uri_;
  OpType type_;
  OperationCondition* condition_;
  bool condition_need_delete_;
  OperationPostProcess* post_process_;
  bool post_process_need_delete_;
};

class InsertOperation : public ContentOperation {
 public:
  InsertOperation(const Uri& uri, OpOnConflict on_conflict)
      : ContentOperation(uri, AOT_INSERT)
        , on_conflict_(on_conflict) {
        }

  virtual ~InsertOperation() { /* virtual destructor */ }

  virtual ContentValues* GetContentValues() { return &values_; }
  virtual Selection* GetSelection()   { assert(0); return NULL; }

  OpOnConflict GetOnConflict() { return on_conflict_; }

 protected:
  ContentValues values_;
  OpOnConflict   on_conflict_;
};

class UpdateOperation : public ContentOperation {
 public:
  UpdateOperation(const Uri& uri, const char* where, va_list ap)
      : ContentOperation(uri, AOT_UPDATE) {
        if (where) {
          selection_.vFormatWhere(where, ap);
        }
      }
#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 3, 4)))
      UpdateOperation(const Uri& uri, const char* where, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  UpdateOperation(
      const Uri& uri, _Printf_format_string_ const char* where, ...);
# else
  UpdateOperation(
      const Uri& uri, __format_string_ const char* where, ...);
#endif
#else
  UpdateOperation(
      const Uri& uri, const char* where, ...);
#endif

  virtual ~UpdateOperation() { /* virtual destructor */ }

  virtual ContentValues* GetContentValues() { return &values_; }
  virtual Selection* GetSelection()   { return &selection_; }

 protected:
  ContentValues values_;
  Selection  selection_;
};

class DeleteOperation : public ContentOperation {
 public:
  DeleteOperation(const Uri& uri, const char* where, va_list ap)
      : ContentOperation(uri, AOT_DELETE) {
        if (where) {
          selection_.vFormatWhere(where, ap);
        }
      }

  virtual ~DeleteOperation() { /* virtual destructor */ }

  virtual ContentValues* GetContentValues() { assert(0); return NULL; }
  virtual Selection* GetSelection()   { return &selection_; }

 protected:
  Selection  selection_;
};

class OperationList {
 public:
  OperationList();
  virtual ~OperationList();

  virtual void AddOperation(
      ContentOperation *opration) {
    operations_.push_back(opration);
  }
  ;
  virtual ContentOperation* NewInsert(
      const Uri& uri, OpOnConflict on_conflict);

#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 3, 4)))
      virtual ContentOperation* NewUpdate(
          const Uri& uri, const char* where, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  virtual ContentOperation* NewUpdate(
      const Uri& uri, _Printf_format_string_ const char* where, ...);
# else
  virtual ContentOperation* NewUpdate(
      const Uri& uri, __format_string const char* where, ...);
# endif /* FORMAT_STRING */
#else
  virtual ContentOperation* NewUpdate(
      const Uri& uri, const char* where, ...);
#endif

#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 3, 4)))
      virtual ContentOperation* NewDelete(
          const Uri& uri, const char* where, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  virtual ContentOperation* NewDelete(
      const Uri& uri, _Printf_format_string_ const char* where, ...);
# else
  virtual ContentOperation* NewDelete(
      const Uri& uri, __format_string const char* where, ...);
# endif /* FORMAT_STRING */
#else
  virtual ContentOperation* NewDelete(
      const Uri& uri, const char* where, ...);
#endif

  //  __attribute__((format(printf, 3, 4)))
  //       virtual ContentOperation* NewUpdate(
  //           const Uri& uri, const char* where, ...);
  //  __attribute__((format(printf, 3, 4)))
  //  virtual ContentOperation* NewDelete(
  //      const Uri& uri, const char* where, ...);

  virtual int32_t GetCount();
  virtual ContentOperation* GetAt(int32_t index);
  virtual void Clear();

 protected:
  std::vector<ContentOperation*> operations_;
};

class IContentProvider;
class IContentResolver {
 public:
  virtual ~IContentResolver() { /* virtual destructor */ }

  virtual err_t Cleanup() = 0;
  virtual err_t Clear(const Uri& uri) = 0;

#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 5, 6)))
      virtual ICursor2* Query(
          const Uri& uri,
          const char* projection[],
          int projection_count,
          const char* selection, ...) = 0;
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  virtual ICursor2* Query(
      const Uri& uri,
      const char* projection[],
      int projection_count,
      _Printf_format_string_ const char* selection, ...) = 0;
# else
  virtual ICursor2* Query(
      const Uri& uri,
      const char* projection[],
      int projection_count,
      __format_string const char* selection, ...) = 0;
# endif /* FORMAT_STRING */
#else
  virtual ICursor2* Query(
      const Uri& uri,
      const char* projection[],
      int projection_count,
      const char* selection, ...) = 0;
#endif

  //  __attribute__((format(printf, 5, 6)))
  //  virtual ICursor2* Query(
  //      const Uri& uri,
  //      const char* projection[],
  //      int projection_count,
  //      const char* selection, ...) = 0;

  virtual ICursor2* vQuery(
      const Uri& uri,
      const char* projection[],
      int projection_count,
      const char* selection, va_list ap) = 0;

  virtual ICursor2* sQuery(
      const Uri& uri,
      const char* projection[],
      int projection_count,
      Selection* selection, const char* sort_order) = 0;

  virtual int32_t Insert(
      const Uri& uri,
      ContentValues* values,
      OpOnConflict on_conflict) = 0;

#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 4, 5)))
      virtual int32_t Update(
          const Uri& uri,
          ContentValues* values,
          const char* where, ...) = 0;
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  virtual int32_t Update(
      const Uri& uri,
      ContentValues* values,
      _Printf_format_string_ const char* where, ...) = 0;
# else
  virtual int32_t Update(
      const Uri& uri,
      ContentValues* values,
      __format_string  const char* where, ...) = 0;
# endif /* FORMAT_STRING */
#else
  virtual int32_t Update(
      const Uri& uri,
      ContentValues* values,
      const char* where, ...) = 0;
#endif

  //  __attribute__((format(printf, 4, 5)))
  //  virtual int32_t Update(
  //      const Uri& uri,
  //      ContentValues* values,
  //      const char* where, ...) = 0;

  virtual int32_t vUpdate(
      const Uri& uri,
      ContentValues* values,
      const char* where, va_list ap) = 0;

  virtual int32_t sUpdate(
      const Uri& uri,
      ContentValues* values,
      Selection* selection) = 0;

#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 3, 4)))
      virtual int32_t Delete(
          const Uri& uri,
          const char* where, ...) = 0;
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  virtual int32_t Delete(
      const Uri& uri,
      _Printf_format_string_ const char* where, ...) = 0;
# else
  virtual int32_t Delete(
      const Uri& uri,
      __format_string const char* where, ...) = 0;
# endif /* FORMAT_STRING */
#else
  virtual int32_t Delete(
      const Uri& uri,
      const char* where, ...) = 0;
#endif

  //  __attribute__((format(printf, 3, 4)))
  //  virtual int32_t Delete(
  //      const Uri& uri,
  //      const char* where, ...) = 0;

  virtual int32_t vDelete(
      const Uri& uri,
      const char* where, va_list ap) = 0;

  virtual int32_t sDelete(
      const Uri& uri,
      Selection* selection) = 0;

  virtual int32_t BulkInsert(
      const Uri& uri,
      ContentValues* values[],
      int32_t values_count,
      OpOnConflict on_conflict) = 0;

  virtual int32_t ApplyBatch(
      // const Uri& uri,
      const char* authority,
      OperationList* op_list) = 0;

#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 5, 6)))
      virtual int32_t QueryInt32(
          const Uri& uri,
          const char* column,
          int32_t default_value,
          const char* selection, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  virtual int32_t QueryInt32(
      const Uri& uri,
      const char* column,
      int32_t default_value,
      _Printf_format_string_ const char* selection, ...);
# else
  virtual int32_t QueryInt32(
      const Uri& uri,
      const char* column,
      int32_t default_value,
      __format_string const char* selection, ...);
# endif /* FORMAT_STRING */
#else
  virtual int32_t QueryInt32(
      const Uri& uri,
      const char* column,
      int32_t default_value,
      const char* selection, ...);
#endif
  //  __attribute__((format(printf, 5, 6)))
  //  virtual int32_t QueryInt32(
  //      const Uri& uri,
  //      const char* column,
  //      int32_t default_value,
  //      const char* selection, ...);

  virtual int32_t sQueryInt32(
      const Uri& uri,
      const char* column,
      int32_t default_value,
      Selection* selection);

#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 5, 6)))
      virtual int64_t QueryInt64(
          const Uri& uri,
          const char* column,
          int64_t default_value,
          const char* selection, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  virtual int64_t QueryInt64(
      const Uri& uri,
      const char* column,
      int64_t default_value,
      _Printf_format_string_ const char* selection, ...);
# else
  virtual int64_t QueryInt64(
      const Uri& uri,
      const char* column,
      int64_t default_value,
      __format_string const char* selection, ...);
# endif /* FORMAT_STRING */
#else
  virtual int64_t QueryInt64(
      const Uri& uri,
      const char* column,
      int64_t default_value,
      const char* selection, ...);
#endif
  //  __attribute__((format(printf, 5, 6)))
  //  virtual int64_t QueryInt64(
  //      const Uri& uri,
  //      const char* column,
  //      int64_t default_value,
  //      const char* selection, ...);

  virtual int64_t sQueryInt64(
      const Uri& uri,
      const char* column,
      int64_t default_value,
      Selection* selection);

#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 4, 5)))
      virtual char* QueryString(
          const Uri& uri,
          const char* column,
          const char* selection, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  virtual char* QueryString(
      const Uri& uri,
      const char* column,
      _Printf_format_string_ const char* selection, ...);
# else
  virtual char* QueryString(
      const Uri& uri,
      const char* column,
      __format_string const char* selection, ...);
# endif /* FORMAT_STRING */
#else
  virtual char* QueryString(
      const Uri& uri,
      const char* column,
      const char* selection, ...);
#endif

  //  __attribute__((format(printf, 4, 5)))
  //  virtual char* QueryString(
  //      const Uri& uri,
  //      const char* column,
  //      const char* selection, ...);

  virtual char* sQueryString(
      const Uri& uri,
      const char* column,
      Selection* selection);

#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 5, 6)))
      virtual bool QueryString(
          const Uri& uri,
          const char* column,
          std::string* result,
          const char* selection, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  virtual bool QueryString(
      const Uri& uri,
      const char* column,
      std::string* result,
      _Printf_format_string_ const char* selection, ...);
# else
  virtual bool QueryString(
      const Uri& uri,
      const char* column,
      std::string* result,
      __format_string const char* selection, ...);
# endif /* FORMAT_STRING */
#else
  virtual bool QueryString(
      const Uri& uri,
      const char* column,
      std::string* result,
      const char* selection, ...);
#endif

  //  __attribute__((format(printf, 5, 6)))
  //  virtual bool QueryString(
  //      const Uri& uri,
  //      const char* column,
  //      std::string* result,
  //      const char* selection, ...);

  virtual bool sQueryString(
      const Uri& uri,
      const char* column,
      std::string* result,
      Selection* selection);

  virtual bool RegisterContentObserver(
      const Uri& uri,
      bool notify_decendents,
      ContentObserver* content_observer) = 0;

  virtual bool UnregisterContentObserver(
      const Uri& uri,
      ContentObserver* content_observer) = 0;

  virtual bool NotifyChange(
      const Uri& uri,
      ContentObserver* content_observer) = 0;

  virtual bool AddProvider(IContentProvider* provider) = 0;
  virtual bool DelProvider(const char* authority, bool is_on_delete) = 0;
  // virtual IContentProvider* FindProvider(const char *authority) = 0;
};

class IContentProvider {
 public:
  IContentProvider();
  virtual ~IContentProvider();

  virtual err_t OnCreate(const char* db_path, const char *key = NULL) = 0;

  virtual const char* GetAuthority() = 0;

  virtual err_t Clear(const Uri& uri) = 0;
  virtual ICursor2* Query(
      const Uri& uri,
      const char* projection[],
      int projection_count,
      const char* where) { assert(false); return NULL; }

  virtual ICursor2* sQuery(
      const Uri& uri,
      const char* projection[],
      int projection_count,
      Selection* selection, const char* sort_order) {
    assert(false); return NULL;
  }

  virtual int32_t Insert(
      const Uri& uri,
      ContentValues* values,
      OpOnConflict on_conflict) { assert(false); return 0; }

  virtual int32_t Update(
      const Uri& uri,
      ContentValues* values,
      const char* where) { assert(false); return 0; }

  virtual int32_t sUpdate(
      const Uri& uri,
      ContentValues* values,
      Selection* selection) { assert(false); return 0; }

  virtual int32_t Delete(
      const Uri& uri,
      const char* where) { assert(false); return 0; }

  virtual int32_t sDelete(
      const Uri& uri,
      Selection* selection) { assert(false); return 0; }

  virtual int32_t BulkInsert(
      const Uri& uri,
      ContentValues* values[],
      int32_t values_count,
      OpOnConflict on_conflict) { assert(false); return 0; }

  virtual int32_t ApplyBatch(
      // URI uri,
      const char* authority,
      OperationList* op_list) { assert(false); return 0; }

  virtual bool RegisterContentObserver(
      const Uri& uri,
      bool notify_decendents,
      ContentObserver* content_observer) {
    assert(false); return false;
  }

  virtual bool UnregisterContentObserver(
      const Uri& uri,
      ContentObserver* content_observer) {
    assert(false); return false;
  }

  virtual bool NotifyChange(
      const Uri& uri,
      ContentObserver* content_observer) {
    assert(false); return false;
  }
  virtual void SetDeleteOnCleanup() = 0;

 protected:
  virtual err_t OnCleanUp() = 0;
  virtual err_t OnDelete() = 0;

};

err_t StartupContentFramework(const char* db_path, const std::vector<std::string> * mactokens= NULL);
err_t ShutdownContentFramework();

#define MAX_SQL_LEN 512

// #define CATEGORY_URI 0x05

IContentResolver* GetContentResolver();

const char SCHEMA_CONTENT[] = "Content";

}  // namespace zs

#endif  // ZISYNC_KERNEL_DATABASE_ICONTENT_H_
