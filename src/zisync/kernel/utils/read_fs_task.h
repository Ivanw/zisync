// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_READ_FS_TASK_H_
#define ZISYNC_KERNEL_UTILS_READ_FS_TASK_H_

#include <string>
#include <vector>
#include <memory>
#include <map>

#include "zisync/kernel/platform/common.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/utils.h"

namespace zs {
using std::string;
using std::unique_ptr;
using std::map;
using std::vector;

class FileStat;
class Tree;

class FsVisitor : public IFsVisitor {
 public:
  FsVisitor(const string &tree_root, int32_t tree_id):
      tree_root_(tree_root), tree_id_(tree_id) {
        FixTreeRoot(&tree_root_);
      }
  ~FsVisitor() {};
  virtual int Visit(const OsFileStat &stat);
  virtual bool IsIgnored(const string &path) const;

  std::vector<std::unique_ptr<FileStat>>* files() { return &files_; }
  void sort();
 private:
  FsVisitor(FsVisitor&);
  void operator=(FsVisitor&);

 protected:
  std::vector<std::unique_ptr<FileStat>> files_;
  string tree_root_;
  const int32_t tree_id_;
};

class ReadFsTask : public OperationCondition {
 public:
  ReadFsTask(const std::string &tree_uuid, const std::string &tree_root, 
             int32_t tree_id, int32_t type);
  virtual err_t Run() = 0;
  bool HasChange() { return has_change; }
  int32_t backup_type() {return backup_type_;}
  void set_backup_type(int32_t type) {backup_type_ = type;}

  static int32_t GetType(const Tree &tree);

  static const int32_t TYPE_RDONLY, TYPE_BACKUP_DST, TYPE_RDWR;
 protected:
  OperationList op_list;
  const std::string tree_uuid_, authority_;
  std::string tree_root_;
  int32_t tree_id_;
  const Uri uri_;
  err_t error;
  bool has_change;
  int32_t type_;
  int32_t backup_type_;
  map<string, vector<unique_ptr<FileStat> > > events_add_;
  vector<unique_ptr<FileStat> > events_rm_;

  void ApplyBatch();
  void ApplyBatchTail();
  err_t AddUpdateFileOp(
      unique_ptr<FileStat> file_in_fs, unique_ptr<FileStat> file_in_db);
  err_t AddInsertFileOp(unique_ptr<FileStat> file_in_fs);
  err_t AddRemoveFileOp(unique_ptr<FileStat> file_in_db);

  virtual bool Evaluate(ContentOperation *cp);

 private:
  ReadFsTask(ReadFsTask&);
  void operator=(ReadFsTask&);

  void AddUpdateOperation(unique_ptr<FileStat> file_in_fs);
  void AddPendingInsertOperation(unique_ptr<FileStat> file_ins);
  void AddPendingRemoveOperation(unique_ptr<FileStat> file_rm);
  void ResolvePendingOperations();


};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_READ_FS_TASK_H_
