// Copyright 2015, zisync.com

#include <memory>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/sync_upload_handler.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"

using std::unique_ptr;
using std::string;

namespace zs {

SyncUploadHandler::SyncUploadHandler(
    const std::string &tree_uuid, const std::string &tmp_root):
    tree_uuid_(tree_uuid), tmp_root_(tmp_root) {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = { TableTree::COLUMN_ROOT };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = '%s' AND %s = %d",
          TableTree::COLUMN_UUID, tree_uuid_.c_str(),
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Noent Tree(%s)", tree_uuid_.c_str());
    return;
  }

  tree_root_ = tree_cursor->GetString(0);
}

bool SyncUploadHandler::OnHandleFile(
    const string &relative_path, const string &real_path,
    const string &sha1) {
  if (tree_root_.empty()) {
    ZSLOG_ERROR("Has fail in Construct");
    return false;
  }
  string target_path = tree_root_ + relative_path;
  if (zs::OsExists(target_path)) {
    ZSLOG_ERROR("Upload target file (%s) exists", target_path.c_str());
    return false;
  }

  int ret = zs::OsRename(real_path, target_path, false);
  if (ret != 0) {
    ZSLOG_ERROR("Rename (%s) => (%s) fail : %s", real_path.c_str(),
                target_path.c_str(), OsGetLastErr());
    return false;
  }
  return true;
}

SyncUploadHandler::~SyncUploadHandler() {
  zs::OsDeleteDirectories(tmp_root_, true);
}

}  // namespace zs
// bool SyncPutHandler::OnHandleUploadFile(
//     int32_t task_id, const std::string &relative_path,
//     const std::string& real_path, const std::string &sha1) {
// }

