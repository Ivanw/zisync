#ifndef ZISYNC_KERNEL_TREE_STATUS_H_
#define ZISYNC_KERNEL_TREE_STATUS_H_

#include "zisync_kernel.h"
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <list>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/status.h"

namespace zs {

class TreeStat : public ITreeStat {
  friend class TreeManager;
 public:
  TreeStat() {
    ConstructMe();
  }
  TreeStat(int32_t tree_id){
    ConstructMe();
    tree_id_ = tree_id;
  }
  virtual ~TreeStat() {
  }

  virtual void OnFileWillIndex(const std::string &file_being_indexed);
  virtual void OnFileIndexed(int32_t count);
  virtual void OnFileTransfered(StatusType type, int32_t count);
  virtual void OnFileSkiped(StatusType type, int32_t count);
  virtual void OnByteTransfered(StatusType type, int64_t nbytes);
  virtual void OnByteSkiped(StatusType type, int64_t nbytes);
  virtual void OnTreeStatBegin(
      StatusType type, int32_t total_files, int64_t total_bytes);
  virtual void OnTreeStatEnd(
      StatusType type, int32_t total_files, int64_t total_bytes);
  void QueryTreeStatus(TreeStatus* status);
  void CalcSpeed();
  void ClearSpeed();

 private:

  AtomicInt32 file_to_index_;
  AtomicInt32 file_to_download_;
  AtomicInt32 file_to_upload_;
  AtomicInt64 byte_to_download_;
  AtomicInt64 byte_to_upload_;
  AtomicInt64 byte_download_time_unit_;
  AtomicInt64 byte_upload_time_unit_;
  AtomicInt64 download_speed_;
  AtomicInt64 upload_speed_;

  bool is_transfering_;
  Mutex mutex_;
  std::string file_indexing_;
  int32_t tree_id_;

  static void Reset(TreeStatus *status);
  
  void PostNotification();
  void ConstructMe() {
    is_transfering_ = false;
  }
};

class TreePairStat : public ITreePairStat {
 public:
   TreePairStat(){
     ConstructMe();
   }
  
  TreePairStat(int32_t local_tree_id, int32_t remote_tree_id) {
    ConstructMe();
    local_tree_id_ = local_tree_id;
    remote_tree_id_ = remote_tree_id;
  }
  
  virtual ~TreePairStat() {}

  virtual void OnFileTransfered(StatusType type, int32_t count) ;
  virtual void OnFileSkiped(StatusType type, int32_t count) ;
  virtual void OnFileTransfer(StatusType type, const string &path) ;
  virtual void OnByteTransfered(StatusType type, int64_t nbytes) ;
  virtual void OnByteSkiped(StatusType type, int64_t nbytes) ;
  virtual void OnTreeStatBegin(
      StatusType type, int32_t total_files, int64_t total_bytes) ;
  virtual void OnTreeStatEnd(
      StatusType type, int32_t total_files, int64_t total_bytes) ;
  virtual void SetStaticFileToUpload(int32_t count) {
    static_file_to_upload_ = count;
  }
  virtual void SetStaticFileToDownload(int32_t count) {
    static_file_to_download_ = count;
  }
  virtual void SetStaticFileConsistent(int32_t count) {
    static_file_consistent_ = count;
  }
  virtual void SetStaticByteToUpload(int64_t byte) {
    static_byte_to_upload_ = byte;
  }
  virtual void SetStaticByteToDownload(int64_t byte) {
    static_byte_to_download_ = byte;
  }
  virtual void SetStaticByteConsistent(int64_t byte) {
    static_byte_consistent_ = byte;
  }

  void QueryTreePairStatus(TreePairStatus* status);
  void CalcSpeed();
  void ClearSpeed();

 private:
  Mutex mutex_;
  int32_t static_file_to_upload_;
  int32_t static_file_to_download_;
  int32_t static_file_consistent_;
  int64_t static_byte_to_upload_;
  int64_t static_byte_to_download_;
  int64_t static_byte_consistent_;

  int32_t file_to_download_;
  int32_t file_to_upload_;
  int64_t byte_to_download_;
  int64_t byte_to_upload_;
  int64_t byte_download_time_unit_;
  int64_t byte_upload_time_unit_;
  int64_t download_speed_;
  int64_t upload_speed_;

  std::string upload_file_;
  std::string download_file_;
  bool is_transfering_;
  int32_t local_tree_id_;
  int32_t remote_tree_id_;
  
  void QueryTreePairStatusNoLock(TreePairStatus *status);
  void PostNotification();
  void ConstructMe() {
    file_to_upload_ = 0;
    file_to_download_ = 0;
    byte_to_download_ = 0;
    byte_to_upload_ = 0;
    download_speed_ = 0;
    upload_speed_ = 0;
    is_transfering_ = false;
  }
};

class TransferList : public ITransferList {
 public:
   TransferList() {
     ConstructMe();
   }
  TransferList(int32_t tree_id){
    ConstructMe();
    tree_id_ = tree_id;
  }
  virtual ~TransferList();

  virtual void OnFileTransfered(StatusType type,
                                int64_t id,
                                const std::string &encode_path);
  virtual void OnFileSkiped(StatusType type,
                            int64_t id,
                            const std::string &encode_path);
  virtual void OnByteTransfered(StatusType type,
                                int64_t nbytes,
                                int64_t id,
                                const std::string &encode_path);
  virtual void OnByteSkiped(StatusType type,
                            int64_t nbytes,
                            int64_t id,
                            const std::string &encode_path);
  virtual void AppendFile(const std::string &local_path,
                          const std::string &remote_path,
                          const std::string &encode_path,
                          int64_t length,
                          int64_t id);
  virtual void OnTransferListBegin(StatusType type,
                              int64_t id);
  virtual void OnTransferListEnd(StatusType type,
                              int64_t id);
  void QueryTransferList(TransferListStatus *transfer_list, int32_t offset,
                         int32_t max_num);
  void CalcSpeed();
 private:
  struct FilePairStat {
    std::string local_path;
    std::string remote_path;
    int64_t length;

    int64_t bytes_to_transfer;
    StatusType type;
  };

  struct FileSpeed {
    int64_t speed;
    int64_t byte_to_transfer_time_unit;
  };

  // id list
  std::list<int64_t> ids_;
  // id => file list
  std::map<int64_t, std::map<std::string, FilePairStat>> file_sort_;
  // id => type
  std::map<int64_t, StatusType> types_;
  // id => file speed
  std::map<int64_t, FileSpeed> speeds_;
  
  int32_t tree_id_;

  OsMutex mutex_;
  err_t AppendListToDatabase(const std::list<FilePairStat> &list);
  void InitFileTransferStat(
      FileTransferStat *stat, const FilePairStat &fp);
  void CollectActiveFile(
      TransferListStatus *list, int32_t max_num);
  void CollectStaticFile(
      TransferListStatus *list, int32_t offset, int32_t max_num);
  bool IsActive(const FilePairStat &stat);
  void QueryTransferListNoLock(TransferListStatus *list, int32_t offset, int32_t max_num);
  void PostNotification();
  void ConstructMe();
};

class TreeManager : public ITreeManager, public IOnTimer {
  friend ITreeManager* GetTreeManager();

 public:
  TreeManager();
  virtual ~TreeManager();

  virtual void OnTaskBegin(
      int32_t local_tree_id, int32_t remote_tree_id, 
      StatusType type, int32_t total_files,
      int64_t total_bytes, int64_t id);
  virtual void OnTaskEnd(
      int32_t local_tree_id, int32_t remote_tree_id, 
      StatusType type, int32_t left_files,
      int64_t left_bytes, int64_t id);
  virtual std::shared_ptr<ITreeStat> GetTreeStat(int32_t tree_id);
  virtual std::shared_ptr<ITreePairStat> GetTreePairStat(
      int32_t local_tree_id, int32_t remote_tree_id);
  virtual std::shared_ptr<ITransferList> GetTransferListStat(int32_t tree_id);

  virtual err_t QueryTreeStatus(int32_t tree_id, TreeStatus* status);
  virtual err_t QueryTreePairStatus(
      int32_t local_tree_id, int32_t remote_tree_id, 
      TreePairStatus* status);
  virtual err_t QueryTransferList(
      int32_t tree_id, TransferListStatus *list,
      int32_t offset, int32_t max_num);
  virtual void OnTimer();

 private:
  err_t StartupTimer();
  err_t ShutdownTimer();

  OsMutex mutex_;
  OsMutex timer_mutex_;
  int32_t reference_count_;

  OsTimer *timer_;
  std::map<int32_t, std::shared_ptr<TreeStat>> status_;
  std::map<std::pair<int32_t /* local_tree_id */, 
      int32_t /* remote_tree_id */>, 
      std::shared_ptr<TreePairStat>> pair_status_;
  std::map<int32_t, std::shared_ptr<TransferList>> transfer_list_stat_;

  static TreeManager s_instance_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_TREE_STATUS_H
