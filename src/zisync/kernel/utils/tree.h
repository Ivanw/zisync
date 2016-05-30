// Copyright 2014, zisync.com
#ifndef ZITREE_KERNEL_UTILS_TREE_H_
#define ZITREE_KERNEL_UTILS_TREE_H_
#include <cstdint>
#include <string>
#include <memory>
#include <cassert>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class ICursor2;
class MsgTree;
class OperationList;

class Tree {
  friend class TreeResumeCondition;
 public:
  Tree();
  virtual ~Tree() {}
 
#ifdef ZS_TEST
#endif

  err_t Create(int32_t sync_id, const std::string &root);
  err_t Save();

  static err_t InitAllTreesModules();

  static err_t DeleteById(int32_t id);

  static Tree* GetLocalTreeByRoot(const std::string &root);
  static Tree* GetByUuid(const std::string& uuid);
  static Tree* GetByUuidWhereStatusNormal(const std::string &uuid);
  static Tree* GetByIdWhereStatusNormal(int32_t id);
#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 1, 2)))
  static Tree* GetBy(const char *selection, ...);
  __attribute__((format(printf, 2, 3)))
      static void QueryBy(
          std::vector<std::unique_ptr<Tree>> *trees, const char *selection, ...);
  __attribute__((format(printf, 1, 2)))
  static int DeleteBy(const char *selection, ...);
  __attribute__((format(printf, 2, 3)))
  static void AppendDeleteBy(OperationList *op, const char *selection, ...);
  __attribute__((format(printf, 2, 3)))
  static void AppendResumeBy(OperationList *op, const char *selection, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  static Tree* GetBy(_Printf_format_string_ const char *selection, ...);
  static void QueryBy(
      std::vector<std::unique_ptr<Tree>> *trees, 
      _Printf_format_string_ const char *selection, ...);
  static int DeleteBy(_Printf_format_string_ const char *selection, ...);
  static void AppendDeleteBy(
      OperationList *op, _Printf_format_string_ const char *selection, ...);
  static void AppendResumeBy(
      OperationList *op, _Printf_format_string_ const char *selection, ...);
# else
  static Tree* GetBy(__format_string const char *selection, ...);
  static void QueryBy(
      std::vector<std::unique_ptr<Tree>> *trees, 
      __format_string const char *selection, ...);
  static int DeleteBy(__format_string const char *selection, ...);
  static void AppendDeleteBy(
      OperationList *op, __format_string const char *selection, ...);
  static void AppendResumeBy(
      OperationList *op, __format_string const char *selection, ...);
# endif /* FORMAT_STRING */
#else
  static Tree* GetBy(const char *selection, ...);
  static void QueryBy(
      std::vector<std::unique_ptr<Tree>> *trees, 
      const char *selection, ...);
  static int DeleteBy(const char *selection, ...);
  static void AppendDeleteBy(OperationList *op, const char *selection, ...);
  static void AppendResumeBy(OperationList *op, const char *selection, ...);
#endif

#if defined(__GNUC__) && (__GNUC__ >= 4)
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
# else
# endif /* FORMAT_STRING */
#else
#endif

  static int32_t GetIdByUuidWhereStatusNormal(const std::string &uuid);
  static int32_t GetSyncIdByIdWhereStatusNormal(int32_t id);

  static bool ExistsWhereStatusNormal(int32_t id);
  static bool ExistsWhereStatusNormalDeviceLocal(int32_t id);
  static bool GetTypeWhereStatusNormalDeviceLocal(
      int32_t tree_id, int32_t *type);

  static bool ExistsByRootWhereStatusNormalDeviceLocal(
      const std::string &root);

  static void QueryBySyncIdWhereStatusNormal(
      int32_t sync_id, std::vector<std::unique_ptr<Tree>> *trees);

  bool IsLocalTree() const;
  bool HasChanged(const MsgTree remote_tree, int32_t device_id) const;
  void AppendTreeUpdateOperation(
    OperationList* tree_op_list,
    const MsgTree remote_tree, 
    int32_t device_id, int32_t backup_type);
  static void AppendTreeInsertOpertion(
    OperationList* tree_op_list,
    const MsgTree remote_tree, 
    int32_t sync_id, int32_t device_id, int32_t backup_type);

  void FixRootForSync();
  err_t SetRoot(const std::string &root);

  int32_t id() const { return id_; }
  int32_t device_id() const { return device_id_; }
  int32_t sync_id() const { return sync_id_; }
  const std::string& uuid() const { return uuid_; }
  const std::string& root() const { return root_; }
  virtual int32_t type() const = 0;
  bool is_sync_enabled() const { return is_sync_enabled_; }
  int32_t status() const { return status_; }
  int32_t root_status() const {return root_status_;}

  void set_root(const std::string &root) { root_ = root; }
  void set_id(int32_t id) { id_ = id; }
  void set_uuid(const std::string &uuid) { uuid_ = uuid; }
  void set_sync_id(int32_t sync_id) { sync_id_ = sync_id; }
  void set_device_id(int32_t device_id) { device_id_ = device_id; }
  void set_status(int32_t status) { status_ = status; }
  void set_last_find(int64_t last_find) { last_find_ = last_find; }
  void set_is_sync_enabled(bool is_sync_enabled) { 
    is_sync_enabled_ = is_sync_enabled; 
  }
  void set_root_status(int32_t root_status) {root_status_ = root_status;}

  virtual err_t ToTreeInfo(TreeInfo *tree_info) const = 0;

  err_t ResumeTreeWatch();
#ifdef ZS_TEST
  err_t TestInitLocalTreeModules() const;
#endif
  
 protected:
  err_t InitLocalTreeModules() const;
  virtual err_t AddSyncList() const = 0; // { assert(false); return ZISYNC_SUCCESS; }
  
  static Tree* Generate(ICursor2 *cursor);
  void ParseFromCursor(ICursor2 *cursor);

  static const char* full_projs[];

  int32_t id_;
  std::string uuid_;
  std::string root_;
  int32_t sync_id_;
  int32_t device_id_;
  int32_t status_;
  int64_t last_find_;
  bool is_sync_enabled_;
  int32_t root_status_;
};

class SyncTree : public Tree {
  friend class Tree;
 public:
  SyncTree() {}
  virtual ~SyncTree() {}
  virtual int32_t type() const;
  /**
   * @return: ZISYNC_ERROR_DEVICE_NOENT if deivce not online
   */
  virtual err_t ToTreeInfo(TreeInfo *tree_info) const;

 private:
  virtual err_t AddSyncList() const;
};

class BackupTree : public Tree {
  friend class Tree;
 public:
  virtual ~BackupTree() {}
  /*  if device is offline still show */
  virtual err_t ToTreeInfo(TreeInfo *tree_info) const;
 protected:
  BackupTree() {}
 private:
  virtual err_t AddSyncList() const ;
};

class BackupSrcTree : public BackupTree {
  friend class Tree;
 public:
  BackupSrcTree() {}
  err_t Create(int32_t sync_id, const std::string &root);
  virtual ~BackupSrcTree() {}
  virtual int32_t type() const;
};                                                                  

class BackupDstTree : public BackupTree {
  friend class Tree;
 public:
  BackupDstTree() {}
  err_t Create(int32_t src_device_id, int32_t sync_id, const std::string &root);
  virtual ~BackupDstTree() {}
  virtual int32_t type() const;
};

}  // namespace zs                                                 

#endif  // ZITREE_KERNEL_UTILS_TREE_H_
