// Copyright 2014, zisync.com

#include <memory>
#include <cstring>
#include <algorithm>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/device.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/sync_list.h"
#include "zisync/kernel/monitor/monitor.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/utils/inner_request.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/utils/platform.h"

namespace zs {

using std::unique_ptr;
using std::vector;

static Mutex tree_create_mutex;

class TreeFileObserver : public ContentObserver {
 public:
  TreeFileObserver(int32_t tree_id, int32_t sync_id):
      tree_id_(tree_id), sync_id_(sync_id)
  { auto_delete_ = true; }
  virtual ~TreeFileObserver() {}

  virtual void* OnQueryChange() {
    return NULL;
  }

  virtual void OnHandleChange(void* changes) {
#ifdef ZS_TEST
    if (!Config::is_auto_sync_enabled()) {
      return;
    }
#endif
    IssueSyncWithLocalTree(sync_id_, tree_id_);
  }
 private:
  int32_t tree_id_, sync_id_;
};

static inline bool HasNestedTree(const string &tree_root, int32_t sync_id) {
  string path;
  if (tree_root != "/") {
    path = tree_root;
  }
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID,
  };
  unique_ptr<ICursor2> cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d AND %s = %d AND ((%s LIKE '%s' || '/%%') OR "
          "('%s' LIKE %s || '/%%'))", TableTree::COLUMN_SYNC_ID, sync_id, 
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID, 
          TableTree::COLUMN_ROOT, path.c_str(), path.c_str(), 
          TableTree::COLUMN_ROOT));
  return cursor->MoveToNext();
}

Tree* Tree::Generate(ICursor2 *cursor) {
  Tree *tree;
  int32_t type = cursor->GetInt32(6);

  if (type == TableTree::BACKUP_NONE) {
    tree = new SyncTree;
  } else if (type == TableTree::BACKUP_SRC) {
    tree = new BackupSrcTree;
  } else {
    tree = new BackupDstTree;
  }
  tree->ParseFromCursor(cursor);
  return tree;
}

void Tree::ParseFromCursor(ICursor2 *cursor) {
  set_status(cursor->GetInt32(5));

  set_id(cursor->GetInt32(0));
  set_uuid(cursor->GetString(1));
  if (status_ != TableTree::STATUS_VCLOCK) {
    set_root(cursor->GetString(2));
    set_device_id(cursor->GetInt32(4));
  }
  set_sync_id(cursor->GetInt32(3));
  set_last_find(cursor->GetInt64(7));
  set_is_sync_enabled(cursor->GetBool(8));
  set_root_status(cursor->GetInt32(9));
}

const char* Tree::full_projs[] = {
  TableTree::COLUMN_ID, TableTree::COLUMN_UUID,
  TableTree::COLUMN_ROOT, TableTree::COLUMN_SYNC_ID,
  TableTree::COLUMN_DEVICE_ID, TableTree::COLUMN_STATUS,
  TableTree::COLUMN_BACKUP_TYPE, TableTree::COLUMN_LAST_FIND, 
  TableTree::COLUMN_IS_ENABLED, TableTree::COLUMN_ROOT_STATUS,
};

Tree::Tree():
    id_(-1), sync_id_(-1), device_id_(-1), 
    status_(TableTree::STATUS_NORMAL), last_find_(TableTree::LAST_FIND_NONE), 
    is_sync_enabled_(true) {}

err_t Tree::InitLocalTreeModules() const {
  IContentResolver *resolver = GetContentResolver();
  /* sync list */
  if (!zs::OsDirExists(root())) {
    ContentValues cv(2);
    cv.Put(TableTree::COLUMN_ROOT_STATUS, TableTree::ROOT_STATUS_REMOVED);
    cv.Put(TableTree::COLUMN_IS_ENABLED, false);
    GetContentResolver()->Update(TableTree::URI, &cv, "%s = %" PRId32,
        TableTree::COLUMN_ID, id());
    ZSLOG_INFO("Tree(%s) root(%s) removed."
        , uuid().c_str(), root().c_str());
    return ZISYNC_ERROR_ROOT_MOVED;
  }
  err_t zisync_ret = AddSyncList();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("AddSyncList(%s) fail : %s", uuid_.c_str(),
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  /* Monitor */
  zisync_ret = Monitor::GetMonitor()->AddWatchDir(root_);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("AddWatchDir(%s) fail", root_.c_str());
    return zisync_ret;
  }

  /* observer */
  TreeFileObserver *observer = new TreeFileObserver(id_, sync_id_);
  if (!resolver->RegisterContentObserver(
          TableFile::GenUri(uuid_.c_str()), true, observer)) {
    delete observer;
    ZSLOG_ERROR("RegisterContentObserver(%s) fail", uuid_.c_str());
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

#ifdef ZS_TEST
err_t Tree::TestInitLocalTreeModules() const {
  return InitLocalTreeModules();
}
#endif

err_t Tree::Create(int32_t sync_id, const std::string &root) {
  if (uuid_.size() == 0) {
    OsGenUuid(&uuid_);
  }
  root_ = root;
  sync_id_ = sync_id;
  device_id_ = TableDevice::LOCAL_DEVICE_ID;
  status_ = TableTree::STATUS_NORMAL;

  IContentResolver* resolver = GetContentResolver();
  assert(root_.size() > 0);

  if (!zs::OsDirExists(root_)) {
    ZSLOG_ERROR("Nonexistent Directory(%s)", root_.c_str());
    return ZISYNC_ERROR_DIR_NOENT;
  }

  string fixed_root = GenFixedStringForDatabase(root_);
  if (HasNestedTree(fixed_root, sync_id_)) {
    ZSLOG_ERROR("Nested Tree(%s)", root_.c_str());
    return ZISYNC_ERROR_NESTED_TREE;
  }

  if (!Sync::ExistsWhereStatusNormal(sync_id)) {
    ZSLOG_ERROR("Noent Sync(%d)", sync_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  }

  /* check whether tree exists */
  { 
    MutexAuto mutex_auto(&tree_create_mutex);
    if (ExistsByRootWhereStatusNormalDeviceLocal(root_)) {
      ZSLOG_ERROR("Tree(%s) exists", root_.c_str());
      return ZISYNC_ERROR_TREE_EXIST;
    }

    /*  provider */
    IContentProvider *provider = new TreeProvider(uuid_.c_str());
    err_t zisync_ret = provider->OnCreate(Config::database_dir().c_str()); 
    if (zisync_ret != ZISYNC_SUCCESS) {
      delete provider;
      ZSLOG_ERROR("Provider(%s) OnCreate fail : %s", 
                  uuid_.c_str(), zisync_strerror(zisync_ret));
      return zisync_ret;
    }
    if (!resolver->AddProvider(provider)) {
      delete provider;
      ZSLOG_ERROR("AddProvider(%s)fail : %s", uuid_.c_str(), 
                  zisync_strerror(zisync_ret));
      zisync_ret = ZISYNC_ERROR_CONTENT;
      return zisync_ret;
    }

    ContentValues tree_cv(5);
    tree_cv.Put(TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
    tree_cv.Put(TableTree::COLUMN_SYNC_ID, sync_id_);
    tree_cv.Put(TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL);
    tree_cv.Put(TableTree::COLUMN_LAST_FIND, TableTree::LAST_FIND_NONE);
    tree_cv.Put(TableTree::COLUMN_UUID, uuid_);
    tree_cv.Put(TableTree::COLUMN_ROOT, root_);
    tree_cv.Put(TableTree::COLUMN_BACKUP_TYPE, type());
    tree_cv.Put(TableTree::COLUMN_IS_ENABLED, is_sync_enabled_);
    tree_cv.Put(TableTree::COLUMN_ROOT_STATUS, TableTree::ROOT_STATUS_NORMAL);
    int32_t tree_id = resolver->Insert(TableTree::URI, &tree_cv, AOC_IGNORE);
    if (tree_id < 0) {
      ZSLOG_ERROR("Insert new tree fail.");
      return ZISYNC_ERROR_CONTENT;
    }
    id_ = tree_id;
  }

  err_t zisync_ret = InitLocalTreeModules();
  if (zisync_ret != ZISYNC_SUCCESS) {
    /*  TODO use rollback */
    ZSLOG_ERROR("InitTreeModules for tree(%s) fail : %s",
                uuid_.c_str(), zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  ZSLOG_INFO("Tree::Create(%s), (%s)", uuid_.c_str(), root_.c_str());

  return ZISYNC_SUCCESS;
}

err_t Tree::DeleteById(int32_t id) {
  err_t zisync_ret = ZISYNC_SUCCESS;
  IContentResolver* resolver = GetContentResolver();
  const char* projections[] = {
    TableTree::COLUMN_UUID, TableTree::COLUMN_ROOT, 
    TableTree::COLUMN_STATUS, TableTree::COLUMN_DEVICE_ID,
  };

  int32_t is_local;
  string tree_root;
  {
    unique_ptr<ICursor2> cursor(resolver->Query(
            TableTree::URI, projections, ARRAY_SIZE(projections),
            "%s = %" PRId32, TableTree::COLUMN_ID, id));
    if (!cursor->MoveToNext() || 
        cursor->GetInt32(2) == TableTree::STATUS_REMOVE) {
      return ZISYNC_ERROR_TREE_NOENT;
    } 
    is_local = cursor->GetInt32(3) == TableDevice::LOCAL_DEVICE_ID;
    tree_root = cursor->GetString(1);
  }
  if (is_local) {
  
    zisync_ret = Monitor::GetMonitor()->DelWatchDir(tree_root);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("DelWatchDir(%s) fail", tree_root.c_str());
      return zisync_ret;
    }

    zisync_ret = SyncList::DelSyncList(id);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("DelSyncList(%d) fail.", id);
      return ZISYNC_ERROR_SYNC_LIST;
    }
  } 

  ContentValues cv(2);
  cv.Put(TableTree::COLUMN_STATUS, TableTree::STATUS_REMOVE);
  cv.Put(TableTree::COLUMN_ROOT_STATUS, TableTree::ROOT_STATUS_NORMAL);
  int num_affected_row = resolver->Update(
      TableTree::URI, &cv, "%s = %" PRId32, TableTree::COLUMN_ID, id);
  if (num_affected_row != 1) {
    ZSLOG_ERROR("Delete Tree fail.");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

void Tree::QueryBySyncIdWhereStatusNormal(
    int sync_id, vector<unique_ptr<Tree>>* trees) {
  trees->clear();

  IContentResolver* resolver = GetContentResolver();

  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, full_projs, ARRAY_SIZE(full_projs),
          "%s = %" PRId32 " AND %s = %d", 
          TableTree::COLUMN_SYNC_ID, sync_id, 
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));

  while (tree_cursor->MoveToNext()) {
    Tree *tree = Generate(tree_cursor.get());
    trees->emplace_back(tree);
  }
}

void Tree::QueryBy(
    std::vector<std::unique_ptr<Tree>> *trees, const char *selection, ...) {
  trees->clear();
  IContentResolver* resolver = GetContentResolver();
  va_list ap;
  va_start(ap, selection);
  string where;
  StringFormatV(&where, selection, ap);
  va_end(ap);

  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, full_projs, ARRAY_SIZE(full_projs),
          "%s", where.c_str()));

  while (tree_cursor->MoveToNext()) {
    Tree *tree = Generate(tree_cursor.get());
    trees->emplace_back(tree);
  }
}

Tree* Tree::GetByUuid(const std::string& uuid) {
  IContentResolver *resolver = GetContentResolver();
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, full_projs, ARRAY_SIZE(full_projs),
          "%s = '%s'", TableTree::COLUMN_UUID, uuid.c_str()));
  if (!tree_cursor->MoveToNext()) {
    // ZSLOG_ERROR("Tree(%s) does not exit", uuid.c_str());
    return NULL;
  }

  return Generate(tree_cursor.get());
}

Tree* Tree::GetByUuidWhereStatusNormal(const string &uuid) {
  IContentResolver *resolver = GetContentResolver();
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, full_projs, ARRAY_SIZE(full_projs),
          "%s = '%s' AND %s = %d", 
          TableTree::COLUMN_UUID, uuid.c_str(), 
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Tree(%s) does not exit", uuid.c_str());
    return NULL;
  }

  return Generate(tree_cursor.get());
}


Tree* Tree::GetByIdWhereStatusNormal(int32_t id) {
  IContentResolver *resolver = GetContentResolver();
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, full_projs, ARRAY_SIZE(full_projs),
          "%s = %d AND %s = %d", 
          TableTree::COLUMN_ID, id, 
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Tree(%d) does not exit", id);
    return NULL;
  }

  return Generate(tree_cursor.get());
}

Tree* Tree::GetBy(const char *selection, ...) {
  IContentResolver *resolver = GetContentResolver();
  va_list ap;
  va_start(ap, selection);
  unique_ptr<ICursor2> tree_cursor(resolver->vQuery(
          TableTree::URI, full_projs, ARRAY_SIZE(full_projs),
          selection, ap));
  va_end(ap);
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Tree does not exit");
    return NULL;
  }

  return Generate(tree_cursor.get());
}

class SingleLocalTreeDeleteCondition : public OperationCondition {
 public:
  SingleLocalTreeDeleteCondition(const Tree &tree):
      tree_root_(tree.root()), tree_id_(tree.id())  {}
  ~SingleLocalTreeDeleteCondition() {}

  virtual bool Evaluate(ContentOperation *op) {
    err_t zisync_ret = Monitor::GetMonitor()->DelWatchDir(tree_root_);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_WARNING("DelWatchDir(%s) fail : %s", tree_root_.c_str(),
                    zisync_strerror(zisync_ret));
    }
    zisync_ret = SyncList::DelSyncList(tree_id_);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_WARNING("DelSyncList(%d) fail : %s", tree_id_,
                    zisync_strerror(zisync_ret));
    }
    return true;
  }
 private:
  string tree_root_;
  int32_t tree_id_;
};

class TreeDeleteCondition : public OperationCondition {
 public:
  TreeDeleteCondition(const string &where) {
    StringFormat(&where_, "( %s ) AND %s = %d", where.c_str(),
                 TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
  }
  ~TreeDeleteCondition() {}

  virtual bool Evaluate(ContentOperation *op) {
    vector<unique_ptr<Tree>> trees;
    Tree::QueryBy(&trees, "%s", where_.c_str());

    for (auto iter = trees.begin(); iter != trees.end(); iter ++) {
      SingleLocalTreeDeleteCondition single(*(*iter));
	  single.Evaluate(NULL);
    }
    return true;
  }

 private:
  string where_;
};

class TreeResumeCondition : public OperationCondition {
 public:
  TreeResumeCondition(const string &where):where_(where) {}
  ~TreeResumeCondition() {}

  virtual bool Evaluate(ContentOperation *op) {
    vector<unique_ptr<Tree>> trees;
    Tree::QueryBy(&trees, "%s", where_.c_str());
    IContentResolver *resolver = GetContentResolver();
    for (auto iter = trees.begin(); iter != trees.end(); iter ++) {
      const Tree &tree = *(*iter);
      IContentProvider *provider = new TreeProvider(tree.uuid().c_str());
      err_t zisync_ret = provider->OnCreate(Config::database_dir().c_str()); 
      if (zisync_ret != ZISYNC_SUCCESS) {
        delete provider;
        ZSLOG_WARNING("Provider(%s) OnCreate fail : %s", 
                      tree.uuid().c_str(), zisync_strerror(zisync_ret));
        continue;
      }
      if (!resolver->AddProvider(provider)) {
        delete provider;
        ZSLOG_ERROR("AddProvider(%s)fail : %s", tree.uuid().c_str(), 
                    zisync_strerror(zisync_ret));
        continue;
      }
      if (tree.IsLocalTree()) {
        zisync_ret = tree.InitLocalTreeModules();
        if (zisync_ret != ZISYNC_SUCCESS) {
          ZSLOG_WARNING("InitLocalTreeModules for tree(%d) fail : %s",
                        tree.id(), zisync_strerror(zisync_ret));
        }
      }
    }
    return true;
  }

 private:
  string where_;
};


int32_t Tree::DeleteBy(const char *selection, ...) {
  IContentResolver *resolver = GetContentResolver();
  va_list ap;
  va_start(ap, selection);
  string where_;
  StringFormatV(&where_, selection, ap);
  va_end(ap);
  string where;
  StringFormat(&where, "( %s ) AND %s = %d", where_.c_str(), 
               TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL);

  TreeDeleteCondition condition(where);
  condition.Evaluate(NULL);

  ContentValues cv(2);
  cv.Put(TableTree::COLUMN_STATUS, TableTree::STATUS_REMOVE);
  cv.Put(TableTree::COLUMN_ROOT_STATUS, TableTree::ROOT_STATUS_NORMAL);
  return resolver->Update(
          TableTree::URI, &cv, "%s", where.c_str());
}

void Tree::AppendDeleteBy(OperationList *op, const char *selection, ...) {
  va_list ap;
  va_start(ap, selection);
  string where_;
  StringFormatV(&where_, selection, ap);
  va_end(ap);
  string where;
  StringFormat(&where, "( %s ) AND %s = %d", where_.c_str(), 
               TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL);
  ContentOperation *co = op->NewUpdate(TableTree::URI, "%s", where.c_str());
  co->GetContentValues()->Put(
      TableTree::COLUMN_STATUS, TableTree::STATUS_REMOVE);
  co->GetContentValues()->Put(
      TableTree::COLUMN_ROOT_STATUS, TableTree::ROOT_STATUS_NORMAL);
  co->SetCondition(new TreeDeleteCondition(where), true);
}

void Tree::AppendResumeBy(OperationList *op, const char *selection, ...) {
  va_list ap;
  va_start(ap, selection);
  string where_;
  StringFormatV(&where_, selection, ap);
  va_end(ap);
  string where;
  StringFormat(&where, "( %s ) AND %s = %d", where_.c_str(), 
               TableTree::COLUMN_STATUS, TableTree::STATUS_REMOVE);
  ContentOperation *co = op->NewUpdate(TableTree::URI, "%s", where.c_str());
  co->GetContentValues()->Put(
      TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL);
  co->SetCondition(new TreeResumeCondition(where), true);
}

int32_t Tree::GetIdByUuidWhereStatusNormal(const std::string &uuid) {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID,
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = '%s' AND %s = %d", 
          TableTree::COLUMN_UUID, uuid.c_str(), 
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Tree(%s) does not exit", uuid.c_str());
    return -1;
  }
  return tree_cursor->GetInt32(0);
}

int32_t Tree::GetSyncIdByIdWhereStatusNormal(int32_t id) {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_SYNC_ID,
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d", 
          TableTree::COLUMN_ID, id, 
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Tree(%d) does not exit", id);
    return -1;
  }
  return tree_cursor->GetInt32(0);
}

bool Tree::GetTypeWhereStatusNormalDeviceLocal(
    int32_t tree_id, int32_t *type) {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_BACKUP_TYPE,
  };
  unique_ptr<ICursor2> cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d AND %s = %d", 
          TableTree::COLUMN_ID, tree_id,
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  if (cursor->MoveToNext()) {
    *type = cursor->GetInt32(0);
    return true;
  } else {
    return false;
  }
}

bool Tree::ExistsWhereStatusNormal(int32_t tree_id) {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID,
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d", 
          TableTree::COLUMN_ID, tree_id, 
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  return tree_cursor->MoveToNext();
}

bool Tree::ExistsWhereStatusNormalDeviceLocal(int32_t id) {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID,
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d AND %s = %d", 
          TableTree::COLUMN_ID, id, 
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  return tree_cursor->MoveToNext();
}

bool Tree::ExistsByRootWhereStatusNormalDeviceLocal(const string &root) {
  IContentResolver *resolver = GetContentResolver();
#ifdef _WIN32
  const char *tree_projs[] = {
    TableTree::COLUMN_ROOT,
  };
  unique_ptr<ICursor2> cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d",
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  std::string lower_root;
  lower_root.resize(root.size());
  std::transform(root.begin(), root.end(), lower_root.begin(), ::tolower);
  while (cursor->MoveToNext()) {
    std::string db_root = cursor->GetString(0);
    std::transform(db_root.begin(), db_root.end(), db_root.begin(), ::tolower);
    if (db_root == lower_root) {
      return true;
    }
  }
  return false;
#else
  const char *tree_projs[] = {
    TableTree::COLUMN_ID,
  };
  unique_ptr<ICursor2> cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = '%s' AND %s = %d AND %s = %d", 
          TableTree::COLUMN_ROOT, GenFixedStringForDatabase(root).c_str(),
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  return cursor->MoveToNext();
#endif
}

err_t Tree::ResumeTreeWatch() {
  IContentResolver *resolver = GetContentResolver();
  err_t zisync_ret;
  IContentProvider *provider = new TreeProvider(uuid().c_str());
  zisync_ret = provider->OnCreate(Config::database_dir().c_str()); 
  if (zisync_ret != ZISYNC_SUCCESS) {
    delete provider;
    ZSLOG_WARNING("Provider(%s) OnCreate fail : %s", 
        uuid().c_str(), zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  if (!resolver->AddProvider(provider)) {
    delete provider;
    ZSLOG_ERROR("AddProvider(%s)fail : %s", uuid().c_str(), 
        zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  if (IsLocalTree()) {
    zisync_ret = InitLocalTreeModules();
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_WARNING("InitLocalTreeModules for tree(%d) fail : %s",
          id(), zisync_strerror(zisync_ret));
    }
  }
  return ZISYNC_SUCCESS;
}

bool Tree::IsLocalTree() const {
  return device_id_ == TableDevice::LOCAL_DEVICE_ID;
}

void Tree::FixRootForSync() {
  FixTreeRoot(&root_);
}

err_t Tree::SetRoot(const string &root) {
  root_ = root;
  if (!zs::OsDirExists(root_)) {
    ZSLOG_ERROR("Nonexistent Directory(%s)", root_.c_str());
    return ZISYNC_ERROR_DIR_NOENT;
  }

  string fixed_root = GenFixedStringForDatabase(root_);
  if (HasNestedTree(fixed_root, sync_id_)) {
    ZSLOG_ERROR("Nested Tree(%s)", root_.c_str());
    return ZISYNC_ERROR_NESTED_TREE;
  }

  if (ExistsByRootWhereStatusNormalDeviceLocal(root_)) {
    ZSLOG_ERROR("Tree(%s) exists", root_.c_str());
    return ZISYNC_ERROR_TREE_EXIST;
  }

  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(1);
  cv.Put(TableTree::COLUMN_ROOT, root);
  int num_affected_row = resolver->Update(
      TableTree::URI, &cv, "%s = %d", TableTree::COLUMN_ID, id_);
  if (num_affected_row != 1) {
    ZSLOG_ERROR("Update Tree(%d) Root fail", id_);
    return ZISYNC_ERROR_CONTENT;
  }
  return ZISYNC_SUCCESS;
}

err_t SyncTree::ToTreeInfo(TreeInfo *tree_info) const {
  tree_info->tree_id = id_;
  tree_info->tree_uuid = uuid_;
  tree_info->tree_root = root_;
  unique_ptr<Device> device(Device::GetByIdWhereStatusOnline(
          device_id_));
  if (!device) {
    return ZISYNC_ERROR_DEVICE_NOENT;
  }
  device->ToDeviceInfo(&tree_info->device);
  tree_info->is_local = IsLocalTree();
  if (IsLocalTree()) {
    tree_info->root_status = 
      root_status() == TableTree::ROOT_STATUS_NORMAL ? TreeRootStatusNormal : TreeRootStatusRemoved;
  }
  tree_info->is_sync_enabled = is_sync_enabled_;
  return ZISYNC_SUCCESS;
}

err_t SyncTree::AddSyncList() const {
  SyncList::AddSyncList(id_, WHITE_SYNC_LIST);
  return ZISYNC_SUCCESS;
}

int32_t SyncTree::type() const {
  return TableTree::BACKUP_NONE;
}

err_t BackupTree::AddSyncList() const {
  SyncList::AddSyncList(id_, NULL_SYNC_LIST);
  return ZISYNC_SUCCESS;
}

err_t BackupTree::ToTreeInfo(TreeInfo *tree_info) const {
  tree_info->tree_id = id_;
  tree_info->tree_uuid = uuid_;
  tree_info->tree_root = root_;
  unique_ptr<Device> device(Device::GetById(device_id_));
  if (!device) {
    return ZISYNC_ERROR_DEVICE_NOENT;
  }
  device->ToDeviceInfo(&tree_info->device);
  tree_info->is_local = IsLocalTree();
  tree_info->is_sync_enabled = is_sync_enabled_;
  return ZISYNC_SUCCESS;
}

err_t BackupSrcTree::Create(
    int32_t sync_id, const string &root) {
  return Tree::Create(sync_id, root);
}

int32_t BackupSrcTree::type() const {
  return TableTree::BACKUP_SRC;
}

static inline bool BackupTreeRootExists(const string &root) {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID,
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
          "%s = %d AND %s = %d AND %s = '%s'", 
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_ROOT, GenFixedStringForDatabase(root).c_str()));
  return tree_cursor->MoveToNext();
}

static inline err_t UpdateDeviceBackupRootInDatabase(
    int32_t device_id, const string &backup_root) {
  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(1);
  assert(backup_root.size() != 0);
  cv.Put(TableDevice::COLUMN_BACKUP_ROOT, backup_root);
  int affected_row_num = resolver->Update(
      TableDevice::URI, &cv, "%s = %d", TableDevice::COLUMN_ID, device_id);
  if (affected_row_num != 1) {
    ZSLOG_ERROR("Update Device(%d) backup_root fail.", device_id);
    return ZISYNC_ERROR_CONTENT;
  }
  return ZISYNC_SUCCESS;
}

static inline err_t CreateTreeRootForBackup(
    int32_t device_id, int32_t sync_id, string *tree_root) {
  IContentResolver *resolver = GetContentResolver();

  unique_ptr<Backup> backup(Backup::GetByIdWhereStatusNormal(sync_id));
  if (!backup) {
    ZSLOG_ERROR("Noent backup(%d)", sync_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  }

  const char *device_projs[] = {
    TableDevice::COLUMN_NAME, TableDevice::COLUMN_BACKUP_ROOT
  };
  unique_ptr<ICursor2> device_cursor(resolver->Query(
          TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
          "%s = %d", TableDevice::COLUMN_ID, device_id));
  if (!device_cursor->MoveToNext()) {
    ZSLOG_ERROR("Noent device(%d)", device_id);
    return ZISYNC_ERROR_DEVICE_NOENT;
  }

  assert(!IsMobileDevice());
  const char *device_backup_root_ = device_cursor->GetString(1);
  const char *device_name = device_cursor->GetString(0);
  string device_backup_root;
  if (device_backup_root_ == NULL || 
      strlen(device_backup_root_) == 0) {
    err_t zisync_ret = GenDeviceRootForBackup(
        device_name, &device_backup_root);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("GenDeviceRootForBackup(%s) fail : %s",
                  device_name, zisync_strerror(zisync_ret));
      return zisync_ret;
    }
    zisync_ret = UpdateDeviceBackupRootInDatabase(
        device_id, device_backup_root);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("UpdateDeviceBackupRootInDatabase(%d) fail : %s",
                  device_id, zisync_strerror(zisync_ret));
      return zisync_ret;
    }
    IssuePushDeviceMeta(device_id);
  } else {
    device_backup_root = device_backup_root_;
  }

  StringFormat(tree_root, "%s/%s", 
               device_backup_root.c_str(), backup->name().c_str());
  int suffix = 0;
  while (BackupTreeRootExists(*tree_root)) {
    suffix ++;
    StringFormat(tree_root, "%s/%s_%d", 
                 device_backup_root.c_str(), backup->name().c_str(), suffix);
  }

  int ret = OsCreateDirectory(*tree_root, false);
  if (ret != 0) {
    ZSLOG_ERROR("OsCreateDirectory(%s) fail : %s",
                tree_root->c_str(), OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }
  return ZISYNC_SUCCESS;
}


err_t BackupDstTree::Create(
    int32_t src_device_id, int32_t sync_id, const string &root) {
  string tree_root;
  bool create_root = false;
  if (root.length() != 0) {
    if (!OsExists(root)) {
      int ret = OsCreateDirectory(root, false);
      if (ret != 0) {
        ZSLOG_ERROR("CreateDirector(%s) fail : %s",
                    root.c_str(), OsGetLastErr());
        return ZISYNC_ERROR_OS_IO;
      }
      create_root = true;
    } else if (OsFileExists(root)) {
      ZSLOG_ERROR("(%s) is a file", root.c_str());
      return ZISYNC_ERROR_NOT_DIR;
    }
    tree_root = root;
  } else {
    err_t zisync_ret = CreateTreeRootForBackup(src_device_id, sync_id, &tree_root);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Create Tree for backup(%d) fail : %s",
                  sync_id, zisync_strerror(zisync_ret));
      return zisync_ret;
    }
    create_root = true;
  }
  err_t zisync_ret = Tree::Create(sync_id, tree_root);
  if (zisync_ret != ZISYNC_SUCCESS) {
    if (create_root) {
      zs::OsDeleteDirectory(tree_root);
    }
    return zisync_ret;
  }
  return ZISYNC_SUCCESS;
}

int32_t BackupDstTree::type() const {
  return TableTree::BACKUP_DST;
}

err_t Tree::InitAllTreesModules() {
  IContentResolver *resolver = GetContentResolver();

  Selection where(
      "%s = %" PRId32 " AND %s = %d", 
      TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
      TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
  unique_ptr<ICursor2> tree_cursor(resolver->sQuery(
          TableTree::URI, full_projs, ARRAY_SIZE(full_projs), 
          &where, TableTree::COLUMN_SYNC_ID));
  while(tree_cursor->MoveToNext()) {
    unique_ptr<Tree> tree(Generate(tree_cursor.get()));
    err_t eno = tree->InitLocalTreeModules();
    if (eno != ZISYNC_SUCCESS && eno != ZISYNC_ERROR_ROOT_MOVED) {
      ZSLOG_ERROR("InitTreeModules for tree(%s) fail : %s",
                  tree->uuid().c_str(), zisync_strerror(eno));
      return eno;
    }
  }
  return ZISYNC_SUCCESS;
}

bool Tree::HasChanged( const MsgTree remote_tree, int32_t device_id) const
{
  assert(uuid_ == remote_tree.uuid());
  assert(device_id_ == device_id || 
         device_id_ == TableDevice::LOCAL_DEVICE_ID ||
         device_id_ == -1);

  int32_t remote_status = remote_tree.is_normal() ?
      TableTree::STATUS_NORMAL : TableTree::STATUS_REMOVE;

  return root_ != remote_tree.root() || status_ != remote_status;
}

class AddProviderCondition : public OperationCondition {
 public:
  AddProviderCondition(const std::string& tree_uuid) 
      : tree_uuid_(tree_uuid) {}
  virtual ~AddProviderCondition() {}

  virtual bool Evaluate(ContentOperation *op) {
    IContentProvider *provider = new TreeProvider(tree_uuid_.c_str());
    err_t zisync_ret = provider->OnCreate(Config::database_dir().c_str());
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Insert Tree fail due to OnCreate Provider(%s/%s) fail : %s", 
                  Config::database_dir().c_str(), tree_uuid_.c_str(),
                  zisync_strerror(zisync_ret));
      delete provider;
      return false;
    }
    if (!GetContentResolver()->AddProvider(provider)) {
      ZSLOG_ERROR("Insert Tree fail due to AddTreeProvider(%s) fail", tree_uuid_.c_str());
      delete provider;
      return false;
    }

    return true;
  }

  std::string tree_uuid_;

};

void Tree::AppendTreeUpdateOperation(
    OperationList* tree_op_list,
    const MsgTree remote_tree, int32_t device_id, int32_t backup_type ) {
  ContentOperation* update = tree_op_list->NewUpdate(
      TableTree::URI, "%s = '%s'", TableTree::COLUMN_UUID, remote_tree.uuid().c_str());
  ContentValues* cv = update->GetContentValues();

  ZSLOG_INFO("Will Update Tree(%s)", remote_tree.uuid().c_str());

  cv->Put(TableTree::COLUMN_STATUS, remote_tree.is_normal() ? 
          TableTree::STATUS_NORMAL : TableTree::STATUS_REMOVE);
  if (device_id_ != TableDevice::LOCAL_DEVICE_ID) {
    cv->Put(TableTree::COLUMN_DEVICE_ID, device_id);
  }
  cv->Put(TableTree::COLUMN_ROOT, remote_tree.root(), true);
  cv->Put(TableTree::COLUMN_BACKUP_TYPE, backup_type);

  if (status_ == TableTree::STATUS_NORMAL) {
    if (!remote_tree.is_normal() && device_id_ == TableDevice::LOCAL_DEVICE_ID) {
      update->SetCondition(new SingleLocalTreeDeleteCondition(*this), true);
    }
  } else {
    if (remote_tree.is_normal()) {
      update->SetCondition(new AddProviderCondition(remote_tree.uuid()), true);
    } 
  }
}

void Tree::AppendTreeInsertOpertion(
    OperationList* tree_op_list,
    const MsgTree remote_tree, 
    int32_t sync_id, int32_t device_id, int32_t backup_type) {
  assert(device_id != TableDevice::LOCAL_DEVICE_ID);

  ZSLOG_INFO("Will Insert Tree(%s)", remote_tree.uuid().c_str());

  ContentOperation* insert = tree_op_list->NewInsert(TableTree::URI, AOC_IGNORE);
  ContentValues* cv = insert->GetContentValues();

  int32_t status = remote_tree.is_normal() ? 
      TableTree::STATUS_NORMAL : TableTree::STATUS_REMOVE;

  cv->Put(TableTree::COLUMN_STATUS, status);
  cv->Put(TableTree::COLUMN_SYNC_ID, sync_id);
  cv->Put(TableTree::COLUMN_DEVICE_ID, device_id);
  cv->Put(TableTree::COLUMN_ROOT, remote_tree.root(), true);
  cv->Put(TableTree::COLUMN_BACKUP_TYPE, backup_type);
  cv->Put(TableTree::COLUMN_UUID, remote_tree.uuid(), true);
  cv->Put(TableTree::COLUMN_LAST_FIND, TableTree::LAST_FIND_NONE);

  if (remote_tree.is_normal()) {
    insert->SetCondition(new AddProviderCondition(remote_tree.uuid()), true);
  }
}

Tree* Tree::GetLocalTreeByRoot(const string &tree_root) {
  IContentResolver *resolver = GetContentResolver();

  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, full_projs, ARRAY_SIZE(full_projs),
          "%s = %d AND %s = %d AND %s = '%s'",
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID, 
          TableTree::COLUMN_ROOT, GenFixedStringForDatabase(tree_root).c_str()));

  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Tree(%s) does not exit", tree_root.c_str());
    return NULL;
  }

  return Generate(tree_cursor.get());
}


}  // namespace zs
