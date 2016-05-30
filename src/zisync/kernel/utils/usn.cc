// Copyright 2014, zisync.com

#include <memory>

#include "zisync/kernel/platform/platform.h"

#include "zisync/kernel/utils/usn.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"

namespace zs {

static AtomicInt64 usn(0);

int64_t GetUsn() {
  return usn.FetchAndInc(1);
}

err_t GetTreeMaxUsnFromContent(const char *tree_uuid, int64_t *tree_max_usn) {
  IContentResolver* resolver = GetContentResolver();
  std::string max_proj;
  StringFormat(&max_proj, "MAX(%s)", TableFile::COLUMN_USN);
  const char *file_projs[] = {
    max_proj.c_str(),
  };
  std::unique_ptr<ICursor2> file_cursor(resolver->Query(
          TableFile::GenUri(tree_uuid), file_projs, 
          ARRAY_SIZE(file_projs), NULL));
  if (file_cursor->MoveToNext()) {
     *tree_max_usn = file_cursor->GetInt64(0);
     return ZISYNC_SUCCESS;
  } else {
    ZSLOG_ERROR("Read MAX Usn of Tree(%s) from Content fail.", tree_uuid);
    return ZISYNC_ERROR_CONTENT;
  }
}

err_t GetMaxUsnFromContent() {
  int64_t max_usn = 0;

  IContentResolver* resolver = GetContentResolver();
  const char* tree_projs[] = {
    TableTree::COLUMN_UUID,
  };
  std::unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
          "%s = %d", TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  while(tree_cursor->MoveToNext()) {
    int64_t tree_max_usn = 0;
    err_t zisync_ret = GetTreeMaxUsnFromContent(
        tree_cursor->GetString(0), &tree_max_usn);
    if (zisync_ret != ZISYNC_SUCCESS) {
      return zisync_ret;
    }
    max_usn = max_usn > tree_max_usn ? max_usn : tree_max_usn;
  }

  usn.set_value(max_usn + 1);
  return ZISYNC_SUCCESS;
}

}  // namespace zs
