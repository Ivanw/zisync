#include <memory>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/platform.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/transfer/fdbuf.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/response.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/query_cache.h"
#include "zisync/kernel/utils/reportdata_handler.h"

namespace zs {

bool ReportDataHandler::GetStatisticDataWithJson(std::string &version,
                                                 std::string& rt_type,
                                                 std::string* json_data) {
  assert(json_data != NULL);
  json_data->clear();

  QuerySyncInfoResult syncinfo;
  QueryBackupInfoResult backupinfo;
  
  QueryCache* query_cache = QueryCache::GetInstance();
  assert(query_cache);
  if (query_cache->QuerySyncInfo(&syncinfo) != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("QuerySyncInfo fail.");
    return false;
  }

  if (query_cache->QueryBackupInfo(&backupinfo) != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("QueryBackupInfo fail.");
    return false;
  }

  std::string device_uuid;
  int32_t device_type;
  device_uuid = Config::device_uuid();
  device_type = GetPlatformWithNum();

  IContentResolver *resolver = GetContentResolver();
  assert(resolver);
  std::string length_proj;
  StringFormat(&length_proj, "SUM(%s)", TableFile::COLUMN_LENGTH);
  const char* projection_count[] = {"COUNT(*)", length_proj.data()};

  std::string sync_items;
  for (size_t i = 0; i < syncinfo.sync_infos.size(); i++) {
    std::string tree_items;
    int32_t file_count = 0;
    int64_t total_size = 0;
    for (size_t k= 0; k < syncinfo.sync_infos[i].trees.size(); k++) {
      {
        std::unique_ptr<ICursor2> cursor_count(resolver->Query(
                TableFile::GenUri(syncinfo.sync_infos[i].trees[k].tree_uuid.c_str()),
                projection_count, 2, "%s = %d and %s = %d",
                TableFile::COLUMN_STATUS, TableFile::STATUS_NORMAL,
                TableFile::COLUMN_TYPE, OS_FILE_TYPE_REG));
        if (!cursor_count->MoveToNext()) {
          continue;
        }
        file_count = cursor_count->GetInt32(0);
        total_size = cursor_count->GetInt64(1);
      }

      if (k != 0) {
        tree_items.append(",");
      }
      StringAppendFormat(&tree_items,
                         "{\"tree_uuid\":\"%s\"," 
                         "\"file_count\":\"%d\","
                         "\"total_size\":\"%" PRId64 "\"}",
                         syncinfo.sync_infos[i].trees[k].tree_uuid.c_str(), file_count,
                         total_size);
    }

    if (i != 0) {
      sync_items.append(",");
    }

    int32_t sync_type = -1;
    if (syncinfo.sync_infos[i].is_share) {
      switch (syncinfo.sync_infos[i].sync_perm) {
        case 1:
          sync_type = 3;
          break;
        case 2:
          sync_type = 4;
          break;
        case 3:
          sync_type = 2;
          break;
        case 4:
          sync_type = 6;
          break;
        default:
          break;
      }
    } else {
      if (syncinfo.sync_infos[i].creator.device_id == TableDevice::LOCAL_DEVICE_ID) {
        sync_type = 5;
      } else {
        sync_type = 1;
      }
    }

    StringAppendFormat(
        &sync_items,
        "{\"sync_uuid\":\"%s\","
        "\"sync_type\":\"%d\","
        "\"trees\":[%s]}",
        syncinfo.sync_infos[i].sync_uuid.c_str(), sync_type, tree_items.c_str());
  }

  std::string backup_items;
  for (size_t i = 0, j = 0; i < backupinfo.backups.size(); i++) {
    if (backupinfo.backups[i].src_tree.is_local == false) {
      continue;
    }
    std::string src_tree_items;
    int32_t file_count = 0;
    int64_t total_size = 0;
    {
      std::unique_ptr<ICursor2> cursor_count(resolver->Query(
              TableFile::GenUri(backupinfo.backups[i].src_tree.tree_uuid.c_str()),
              projection_count, 2, "%s = %d and %s = %d",
              TableFile::COLUMN_STATUS, TableFile::STATUS_NORMAL,
              TableFile::COLUMN_TYPE, OS_FILE_TYPE_REG));
      if (!cursor_count->MoveToNext()) {
        continue;
      }
      file_count = cursor_count->GetInt32(0);
      total_size = cursor_count->GetInt64(1);
    }

    std::string dest_tree_items;
    for (size_t k = 0; k < backupinfo.backups[i].target_trees.size(); k++) {
      std::string device_uuid; 
      resolver->QueryString(
          TableDevice::URI, TableDevice::COLUMN_UUID, &device_uuid,
          "%s = %d", TableDevice::COLUMN_ID,
          backupinfo.backups[i].target_trees[k].device.device_id);
      int sync_mode = resolver->QueryInt32(
          TableSyncMode::URI, TableSyncMode::COLUMN_SYNC_MODE, -1,
          "%s = %d and %s = %d",
          TableSyncMode::COLUMN_LOCAL_TREE_ID,
          backupinfo.backups[i].src_tree.tree_id,
          TableSyncMode::COLUMN_REMOTE_TREE_ID, 
          backupinfo.backups[i].target_trees[k].tree_id);
      sync_mode++;  // appear sync_mode number to STAT server data
      std::string tree_uuid;
      tree_uuid = backupinfo.backups[i].target_trees[k].tree_uuid;
      std::string sync_mode_str;

      if (k != 0) {
        dest_tree_items.append(",");
      }
      StringAppendFormat(&dest_tree_items,
                         "{\"device_uuid\":\"%s\","
                         "\"tree_uuid\":\"%s\","
                         "\"backup_mode\":\"%d\"}",
                         device_uuid.c_str(),
                         tree_uuid.c_str(),
                         sync_mode);
                         //sync_mode_str.c_str());
    }

    StringAppendFormat(
        &src_tree_items,
        "{\"tree_uuid\":\"%s\","
        "\"file_count\":\"%d\","
        "\"total_size\":\"%" PRId64 "\","
        "\"dest_trees\":[%s]}",
        backupinfo.backups[i].src_tree.tree_uuid.c_str(), file_count,
        total_size, dest_tree_items.c_str());

    int32_t sync_id;
    std::string sync_uuid;
    sync_id = resolver->QueryInt32(
        TableTree::URI, TableTree::COLUMN_SYNC_ID, -1,
        "%s = %d", TableTree::COLUMN_ID,
        backupinfo.backups[i].src_tree.tree_id);
    resolver->QueryString(
        TableSync::URI, TableSync::COLUMN_UUID, &sync_uuid,
        "%s = %d", TableSync::COLUMN_ID, sync_id);

    if (j != 0) {
      backup_items.append(",");
    }
    StringAppendFormat(
        &backup_items,
        "{\"sync_uuid\":\"%s\","
        "\"src_tree\":[%s]}",
        sync_uuid.c_str(), src_tree_items.c_str());
    j++;
  }

  int event_type = -1;
  if (rt_type == "start")
    event_type = 1;
  else if (rt_type == "period")
    event_type = 2;
  else if (rt_type == "stop")
    event_type = 3;

  StringFormat(json_data,
               "{\"device_uuid\":\"%s\","
               "\"device_type\":\"%d\","
               "\"version\":\"%s\","
               "\"version_number\":\"%d\","
               "\"event\":\"%d\","
               "\"syncs\":[%s],"
               "\"backups\":[%s]}",
               device_uuid.c_str(), device_type, version.c_str(),
               version_number, event_type, sync_items.c_str(),
               backup_items.c_str());

  return true;
}

err_t ReportDataHandler::SendStatisticData(std::string& version,
                                           std::string& rt_type,
                                           int timeout_sec) {
  timeval recv_to;
  timeval send_to;

  std::string json_data;
  if (GetStatisticDataWithJson(version, rt_type, &json_data) == false) {
    ZSLOG_ERROR("GetStatisticDataWithJson fail.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  std::string buffer;
  StringFormat(&buffer, 
               "POST /stat/%s HTTP/1.1\r\n"
               "Host: %s\r\n"
               "Content-Type: text/json\r\n"
               "Content-Length: %d\r\n" "\r\n" "%s",
               Config::device_uuid().c_str(), kReportHost,
               static_cast<int>(json_data.size()), json_data.c_str());


  string uri;
  StringFormat(&uri, "tcp://%s:80", kReportHost);
  OsTcpSocket client_socket(uri.c_str());
  if (!(timeout_sec < 0)) {
	  recv_to.tv_sec = timeout_sec;
	  recv_to.tv_usec = 0;
	  if (client_socket.SetSockOpt(SOL_SOCKET, SO_RCVTIMEO, &recv_to,
		  sizeof(recv_to)) == -1) {
			  ZSLOG_ERROR("SetSockOpt fail.");
	  }
	  send_to.tv_sec = timeout_sec;
	  send_to.tv_usec = 0;
	  if (client_socket.SetSockOpt(SOL_SOCKET, SO_SNDTIMEO, &send_to,
		  sizeof(send_to)) == -1) {
			  ZSLOG_ERROR("SetSockOpt fail.");
	  }
  }
  if (client_socket.Connect() == -1) {
    ZSLOG_ERROR("OsTcpSocket Connect failed: %s", client_socket.uri().data());
    return ZISYNC_ERROR_OS_SOCKET;
  }
  if (client_socket.Send(buffer, 0) < 0) {
    ZSLOG_ERROR("OsTcpSocket Send failed!");
    return ZISYNC_ERROR_OS_SOCKET;
  }
  client_socket.Shutdown("w");

  fdbuf fd_buf(&client_socket);
  std::istream in(&fd_buf);

  int code = -1;
  std::string line;
  std::getline(in, line, '\n');
  if (!line.empty()) {
	  int ret = sscanf(line.c_str(), "HTTP/1.1 %d ", &code);
	  if (!(ret > 0)) {
		  ZSLOG_ERROR("sscanf fail.");
		  return ZISYNC_ERROR_HTTP_RETURN_ERROR;
	  }
  }

  if (code != 200) {
    ZSLOG_ERROR("Report data receive status not OK.");
    return ZISYNC_ERROR_HTTP_RETURN_ERROR;
  }

  return ZISYNC_SUCCESS;
}

err_t ReportDataHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  std::string version, rt_type;
  version = request_msg_.version();
  rt_type = request_msg_.rt_type();

  err_t zisync_ret = SendStatisticData(version, rt_type, -1);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("SendStatisticData failed.");
  }

  ReportDataResponse response_;
  err_t not_ret = response_.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

};  // namespace zs
