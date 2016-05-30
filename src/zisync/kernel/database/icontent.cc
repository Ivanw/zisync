/* Copyright [2014] <zisync.com> */

#include <string.h>
#include <assert.h>
#include <stdarg.h>

#include <memory>
#include <string>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/zslog.h"

namespace zs {

ContentValues::ContentValues() {
}

ContentValues::ContentValues(int32_t count) {
  elements_.reserve(count);
}

ContentValues::~ContentValues() {
  for (auto it = duplicates_.begin();
       it != duplicates_.end(); it++) {
    if (*it) {
      free(*it);
    }
  }

  duplicates_.clear();
}

void ContentValues::Put(const char* key, bool value) {
	ContentValue cv;
	cv.type_ = AVT_INT32;
	cv.key_ = key;
	cv.i32_ = value ? 1 : 0;
	elements_.push_back(cv);
}

void ContentValues::Put(const char* key, int32_t value) {
  ContentValue cv;
  cv.type_ = AVT_INT32;
  cv.key_ = key;
  cv.i32_ = value;
  elements_.push_back(cv);
}

void ContentValues::Put(const char* key, int64_t value) {
  ContentValue cv;
  cv.type_ = AVT_INT64;
  cv.key_ = key;
  cv.i64_ = value;
  elements_.push_back(cv);
}
void ContentValues::Put(const char* key, const char* value, bool duplicate) {
  ContentValue cv;
  cv.type_ = AVT_TEXT;
  cv.key_ = key;
  if (duplicate) {
    char* dup_value = strdup(value);
    duplicates_.push_back(dup_value);
    cv.str_ = dup_value;
  } else {
    cv.str_ = value;
  }
  elements_.push_back(cv);
}

void ContentValues::Put(const char* key, const std::string& value, bool duplicate) {
  ContentValue cv;
  cv.type_ = AVT_TEXT;
  cv.key_ = key;
  if (duplicate) {
    char* dup_value = strdup(value.data());
    duplicates_.push_back(dup_value);
    cv.str_ = dup_value;
  } else {
    cv.str_ = value.data();
  }
  elements_.push_back(cv);
}

void ContentValues::Put(
    const char* key,
    const void* value,
    int32_t nbytes,
    bool duplicate) {
  ContentValue cv;
  cv.type_ = AVT_BLOB;
  cv.key_ = key;
  cv.count_ = nbytes;
  if (duplicate) {
    void* base = malloc(nbytes);
    assert(base);
    memcpy(base, value, nbytes);
    duplicates_.push_back(base);
    cv.base_ = base;
  } else {
    cv.base_ = value;
  }
  elements_.push_back(cv);
}

void ContentValues::PutLiteral(
    const char* key, const char* value, bool duplicate) {
  ContentValue cv;
  cv.type_ = AVT_LITERAL;
  cv.key_ = key;
  if (duplicate) {
    char* dup_value = strdup(value);
    duplicates_.push_back(dup_value);
    cv.str_ = dup_value;
  } else {
    cv.str_ = value;
  }
  elements_.push_back(cv);
}

int32_t ContentValues::GetCount() {
  return (int32_t) elements_.size();
}

const char* ContentValues::GetKey(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  return elements_[index].key_;
}

ValueType ContentValues::GetType(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  return elements_[index].type_;
}

int32_t ContentValues::GetInt32(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  assert(elements_[index].type_ == AVT_INT32);
  return elements_[index].i32_;
}

int64_t ContentValues::GetInt64(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  assert(elements_[index].type_ == AVT_INT64);
  return elements_[index].i64_;
}

const char* ContentValues::GetString(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  // printf("%d %d %d\n", elements_[index].type_, AVT_TEXT, AVT_LITERAL);
  assert(elements_[index].type_ == AVT_TEXT ||
         elements_[index].type_ == AVT_LITERAL);
  return elements_[index].str_;
}

const void* ContentValues::GetBlobBase(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  assert(elements_[index].type_ == AVT_BLOB);
  return elements_[index].base_;
}

int32_t ContentValues::GetBlobSize(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  assert(elements_[index].type_ == AVT_BLOB);
  return elements_[index].count_;
}

Selection::Selection() {
}

Selection::Selection(const char* where, ...) {
  va_list ap;
  va_start(ap, where);
  StringFormatV(&where_, where, ap);
  va_end(ap);
}

Selection::~Selection() {
  for (auto it = duplicates_.begin();
       it != duplicates_.end(); it++) {
    if (*it) {
      free(*it);
    }
  }

  duplicates_.clear();
}

void Selection::vFormatWhere(const char* where, va_list ap) {
  StringFormatV(&where_, where, ap);
}

void Selection::Add(int32_t value) {
  Argument av;
  av.type_ = AVT_INT32;
  av.i32_ = value;
  elements_.push_back(av);
}

void Selection::Add(int64_t value) {
  Argument av;
  av.type_ = AVT_INT64;
  av.i64_ = value;
  elements_.push_back(av);
}
void Selection::Add(const char* value, bool duplicate /* = false */) {
  Argument av;
  av.type_ = AVT_TEXT;
  if (duplicate) {
    char* dup_value = strdup(value);
    duplicates_.push_back(dup_value);
    av.value_ = dup_value;
  } else {
    av.value_ = value;
  }
  elements_.push_back(av);
}

void Selection::Add(
    void* value, int32_t nbytes, bool duplicate /* = false */) {
  Argument av;
  av.type_ = AVT_BLOB;
  av.count_ = nbytes;
  if (duplicate) {
    void* base = malloc(nbytes);
    assert(base);
    memcpy(base, value, nbytes);
    duplicates_.push_back(base);
    av.base_ = base;
  } else {
    av.base_ = value;
  }
  elements_.push_back(av);
}

// void Selection::AddLiteral(const char* value)
// {
//  Argument av;
//  av.type_ = AVT_LITERAL;
//  av.value_ = value;
//  elements_.push_back(av);
// }

int32_t Selection::GetCount() {
  return (int32_t) elements_.size();
}

const char* Selection::GetWhere() {
  if (where_.empty()) {
    return NULL;
  }
  return where_.c_str();
}

ValueType Selection::GetType(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  return elements_[index].type_;
}

int32_t Selection::GetInt32(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  assert(elements_[index].type_ == AVT_INT32);
  return elements_[index].i32_;
}

int64_t Selection::GetInt64(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  assert(elements_[index].type_ == AVT_INT64);
  return elements_[index].i64_;
}

const char*  Selection::GetString(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  assert(elements_[index].type_ == AVT_TEXT);
  return elements_[index].value_;
}

void*  Selection::GetBlobBase(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  assert(elements_[index].type_ == AVT_BLOB);
  return elements_[index].base_;
}

int32_t Selection::GetBlobSize(int32_t index) {
  assert(index >= 0 && index < (int32_t)elements_.size());
  assert(elements_[index].type_ == AVT_BLOB);
  return elements_[index].count_;
}

/*  define in content_provider.cc to ensure then construct order */
// IContentProvider IContentProvider::s_provider_list_head(true);

IContentProvider::IContentProvider() {
}
IContentProvider::~IContentProvider() {
}

int32_t IContentResolver::QueryInt32(
    const Uri& uri,
    const char* column,
    int32_t default_value,
    const char* selection, ...) {
  assert(column != NULL);

  const char* projection[] = { column };

  va_list ap;
  va_start(ap, selection);
  std::unique_ptr<ICursor2> cursor(
      vQuery(uri, projection, 1, selection, ap));
  va_end(ap);

  if (!cursor->MoveToNext()) {
    return default_value;
  } else {
    return cursor->GetInt32(0);
  }
}

int32_t IContentResolver::sQueryInt32(
    const Uri& uri,
    const char* column,
    int32_t default_value,
    Selection* selection) {
  assert(column != NULL);

  const char* projection[] = { column };


  std::unique_ptr<ICursor2> cursor(
      sQuery(uri, projection, 1, selection, NULL));

  if (!cursor->MoveToNext()) {
    return default_value;
  } else {
    return cursor->GetInt32(0);
  }
}

int64_t IContentResolver::QueryInt64(
    const Uri& uri,
    const char* column,
    int64_t default_value,
    const char* selection, ...) {
  assert(column != NULL);

  const char* projection[] = { column };

  va_list ap;
  va_start(ap, selection);
  std::unique_ptr<ICursor2> cursor(
      vQuery(uri, projection, 1, selection, ap));
  va_end(ap);

  if (!cursor->MoveToNext()) {
    return default_value;
  } else {
    return cursor->GetInt64(0);
  }
}

int64_t IContentResolver::sQueryInt64(
    const Uri& uri,
    const char* column,
    int64_t default_value,
    Selection* selection) {
  assert(column != NULL);

  const char* projection[] = { column };

  std::unique_ptr<ICursor2> cursor(
      sQuery(uri, projection, 1, selection, NULL));

  if (!cursor->MoveToNext()) {
    return default_value;
  } else {
    return cursor->GetInt64(0);
  }
}

char* IContentResolver::QueryString(
    const Uri& uri,
    const char* column,
    const char* selection, ...) {
  assert(column != NULL);

  const char* projection[] = { column };

  va_list ap;
  va_start(ap, selection);
  std::unique_ptr<ICursor2> cursor(
      vQuery(uri, projection, 1, selection, ap));
  va_end(ap);

  if (!cursor->MoveToNext()) {
    return NULL;
  } else {
    return strdup(cursor->GetString(0));
  }
}

char* IContentResolver::sQueryString(
    const Uri& uri,
    const char* column,
    Selection* selection) {
  assert(column != NULL);

  const char* projection[] = { column };

  std::unique_ptr<ICursor2> cursor(
      sQuery(uri, projection, 1, selection, NULL));

  if (!cursor->MoveToNext()) {
    return NULL;
  } else {
    return strdup(cursor->GetString(0));
  }
}


bool IContentResolver::QueryString(
    const Uri& uri,
    const char* column,
    std::string* result,
    const char* selection, ...) {
  assert(column != NULL);

  const char* projection[] = { column };

  va_list ap;
  va_start(ap, selection);
  std::unique_ptr<ICursor2> cursor(
      vQuery(uri, projection, 1, selection, ap));
  va_end(ap);

  if (!cursor->MoveToNext()) {
    return false;
  } else {
    *result = cursor->GetString(0);
    return true;
  }
}

bool IContentResolver::sQueryString(
    const Uri& uri,
    const char* column,
    std::string* result,
    Selection* selection) {
  assert(column != NULL);

  const char* projection[] = { column };

  std::unique_ptr<ICursor2> cursor(
      sQuery(uri, projection, 1, selection, NULL));

  if (!cursor->MoveToNext()) {
    return false;
  } else {
    *result = cursor->GetString(0);
    return true;
  }
}

UpdateOperation::UpdateOperation(const Uri& uri, const char* where, ...)
  : ContentOperation(uri, AOT_UPDATE) {
    va_list ap;
    va_start(ap, where);
    selection_.vFormatWhere(where, ap);
    va_end(ap);
  }


OperationList::OperationList() {
}

OperationList::~OperationList() {
  std::vector<ContentOperation*> tmp;
  operations_.swap(tmp);

  for (auto it = tmp.begin(); it != tmp.end(); it++) {
    if (*it) {
      delete (*it);
    }
  }

  tmp.clear();
}

void OperationList::Clear() {
  std::vector<ContentOperation*> tmp;
  operations_.swap(tmp);

  for (auto it = tmp.begin(); it != tmp.end(); it++) {
    if (*it) {
      delete (*it);
    }
  }

  tmp.clear();
}

ContentOperation* OperationList::NewInsert(
    const Uri& uri, OpOnConflict on_conflict) {
  InsertOperation* insert = new InsertOperation(uri, on_conflict);
  operations_.push_back(insert);

  return insert;
}

ContentOperation* OperationList::NewUpdate(
    const Uri& uri, const char* where, ...) {
  UpdateOperation* update = NULL;

  va_list ap;
  va_start(ap, where);
  update = new UpdateOperation(uri, where, ap);
  va_end(ap);

  operations_.push_back(update);

  return update;
}

ContentOperation* OperationList::NewDelete(
    const Uri& uri, const char* where, ...) {
  DeleteOperation* delete_operation = NULL;

  va_list ap;
  va_start(ap, where);
  delete_operation = new DeleteOperation(uri, where, ap);
  va_end(ap);

  operations_.push_back(delete_operation);

  return delete_operation;
}

int32_t OperationList::GetCount() {
  return (int32_t) operations_.size();
}

ContentOperation* OperationList::GetAt(int32_t index) {
  if (index >= 0 && index < (int32_t)operations_.size()) {
    return operations_.at(index);
  } else {
    assert(false);
    return NULL;
  }
}


Uri::Uri(const char* uri, ...) {
  va_list ap;
  va_start(ap, uri);
  StringFormatV(&uri_, uri, ap);
  va_end(ap);

  size_t start = 0;
  size_t index = uri_.find("://");
  if (index != std::string::npos) {
    schema_ = uri_.substr(0, index);
    start = index + 3;
  }

  index = uri_.find('/', start);
  if (index != std::string::npos) {
    authority_ = uri_.substr(start, index - start);
    path_ = uri_.substr(index);
  } else {
    authority_ = uri_.substr(start);
    path_ = "/";
  }
}

Uri::Uri(const char* schema, const char* authority, const char* path)
    : schema_(schema), authority_(authority), path_(path) {
  StringFormat(&uri_, "%s://%s%s", schema, authority, path);
}

const char* Uri::AsString() const {
  return uri_.c_str();
}

const char* Uri::GetSchema() const {
  return schema_.c_str();
}

const char* Uri::GetAuthority() const {
  return authority_.c_str();
}

const char* Uri::GetPath() const {
  return path_.c_str();
}

std::string Uri::GetLastPathSegment() const {
  // return m_strLastPathSegment;
  if (path_ == "/") {
    return std::string();
  }

  size_t index = path_.rfind('/');

  assert(index != std::string::npos);

  return path_.substr(index + 1);
}

ContentObserver::ContentObserver() {
}

ContentObserver::~ContentObserver() {
}

void ContentObserver::DispatchChange(bool /* is_self_change */) {
  class COnChangeRunable : public IRunnable {
   public:
    COnChangeRunable(
        ContentObserver* observer, void* changes)
        : observer_(observer), changes_(changes) {
    }

    virtual int Run() {
      if (observer_) {
        observer_->OnHandleChange(changes_);
        return ZISYNC_SUCCESS;
      }
      return ZISYNC_ERROR_GENERAL;
    }

   protected:
    ContentObserver* observer_;
    void* changes_;
  };

  IHandler* handler = GetHandler();
  if (!handler) {
    OnHandleChange(OnQueryChange());
  } else {
    void* changes = OnQueryChange();
    handler->Post(new COnChangeRunable(this, changes));
  }
}

}  // namespace zs
