noinst_LIBRARIES = libkernel.a 
libdir=${prefix}
lib_LIBRARIES = libzskernel.a

if ZS_ANDROID
PROTOC = @protoc
else
PROTOC = @THIRD_PART_BIN_DIR@/protoc
endif

AUTOMAKE_OPTIONS = -Wno-override

libkernel_a_SOURCES =                 \
    zisync/kernel/proto/kernel.pb.cc zisync/kernel/proto/kernel.pb.h \
	zisync/kernel/proto/verify.pb.cc zisync/kernel/proto/verify.pb.h \
	zisync/kernel/transfer/transfer.pb.h zisync/kernel/transfer/transfer.pb.cc \
	zisync/kernel/zslog.h zisync/kernel/zslog.cc zisync/kernel/utils/default_logger.cc \
	zisync/kernel/format.h \
	zisync/kernel/format.cc \
    zisync/kernel/kernel.cc                            \
	zisync/kernel/tree_status.h \
	zisync/kernel/tree_status.cc \
	zisync/kernel/permission.h zisync/kernel/permission.cc \
	zisync/kernel/platform/platform.cc				 \
    zisync/kernel/database/icore.cc                    \
    zisync/kernel/database/icontent.cc                 \
    zisync/kernel/database/xsqlite.cc                  \
    zisync/kernel/database/content_provider.cc         \
    zisync/kernel/database/content_resolver.cc		 \
	zisync/kernel/database/table.h zisync/kernel/database/table.cc \
	zisync/kernel/database/content.h zisync/kernel/database/content.cc \
	zisync/kernel/transfer/tar_helper.h \
	zisync/kernel/transfer/task_monitor.h \
	zisync/kernel/transfer/task_monitor.cc \
    zisync/kernel/utils/url.h zisync/kernel/utils/url.cc \
	zisync/kernel/utils/base64.h  zisync/kernel/utils/base64.cc \
	zisync/kernel/utils/request.h zisync/kernel/utils/response.h \
	zisync/kernel/utils/zmq.h zisync/kernel/utils/zmq.cc \
	zisync/kernel/utils/cipher.h zisync/kernel/utils/cipher.cc \
	zisync/kernel/utils/configure.h zisync/kernel/utils/configure.cc \
	zisync/kernel/utils/error.cc zisync/kernel/utils/utils.h \
	zisync/kernel/utils/sha.h zisync/kernel/utils/sha.cc \
	zisync/kernel/utils/vector_clock.h zisync/kernel/utils/vector_clock.cc \
	zisync/kernel/utils/usn.h zisync/kernel/utils/usn.cc \
	zisync/kernel/utils/read_fs_task.h zisync/kernel/utils/read_fs_task.cc \
	zisync/kernel/utils/file_stat.h zisync/kernel/utils/file_stat.cc \
	zisync/kernel/utils/event_notifier.h zisync/kernel/utils/event_notifier.cc \
	zisync/kernel/utils/issue_request.h zisync/kernel/utils/issue_request.cc \
	zisync/kernel/worker/worker.h zisync/kernel/worker/worker.cc \
	zisync/kernel/worker/inner_worker.h zisync/kernel/worker/inner_worker.cc \
	zisync/kernel/worker/outer_worker.h zisync/kernel/worker/outer_worker.cc \
	zisync/kernel/worker/refresh_worker.h zisync/kernel/worker/refresh_worker.cc \
	zisync/kernel/worker/sync_worker.h zisync/kernel/worker/sync_worker.cc \
	zisync/kernel/worker/sync_file_task.h zisync/kernel/worker/sync_file_task.cc \
	zisync/kernel/worker/sync_file.h zisync/kernel/worker/sync_file.cc \
	zisync/kernel/sync_tree_agent.h zisync/kernel/sync_tree_agent.cc \
	zisync/kernel/utils/message.h \
	zisync/kernel/utils/message.cc \
	zisync/kernel/utils/x509_cert.h \
	zisync/kernel/utils/x509_cert.cc \
	zisync/kernel/utils/pkcs12_cert.h \
	zisync/kernel/utils/pkcs12_cert.cc \
	zisync/kernel/utils/transfer.h \
	zisync/kernel/utils/sync_list.cc zisync/kernel/utils/sync_list.h \
	zisync/kernel/utils/trie.cc zisync/kernel/utils/trie.h \
	zisync/kernel/utils/abort.cc zisync/kernel/utils/abort.h \
	zisync/kernel/utils/context.cc zisync/kernel/utils/content.h \
	zisync/kernel/utils/discover_device.cc zisync/kernel/utils/discover_device.h \
	zisync/kernel/utils/tree_mutex.cc zisync/kernel/utils/tree_mutex.h \
	zisync/kernel/utils/inner_request.cc zisync/kernel/utils/inner_request.h \
	zisync/kernel/utils/download.cc zisync/kernel/utils/download.h \
	zisync/kernel/utils/upload.cc zisync/kernel/utils/upload.h \
	zisync/kernel/utils/updownload.cc zisync/kernel/utils/updownload.h \
	zisync/kernel/utils/normalize_path.h zisync/kernel/utils/normalize_path.cc \
	zisync/kernel/utils/normalize_path_linux.cc zisync/kernel/utils/normalize_path_window.cc \
	zisync/kernel/utils/utils.h zisync/kernel/utils/utils.cc \
	zisync/kernel/utils/ignore.h zisync/kernel/utils/ignore.cc \
	zisync/kernel/utils/device.h zisync/kernel/utils/device.cc \
	zisync/kernel/utils/sync.h zisync/kernel/utils/sync.cc \
	zisync/kernel/utils/tree.h zisync/kernel/utils/tree.cc \
	zisync/kernel/utils/rename.cc zisync/kernel/utils/rename.h \
	zisync/kernel/utils/query_cache.cc zisync/kernel/utils/query_cache.h \
	zisync/kernel/utils/sync_updater.cc zisync/kernel/utils/sync_updater.h \
	zisync/kernel/utils/tree_traverse.cc zisync/kernel/utils/tree_traverse.h \
	zisync/kernel/utils/sync_put_handler.h zisync/kernel/utils/sync_put_handler.cc \
	zisync/kernel/utils/sync_upload_handler.h zisync/kernel/utils/sync_upload_handler.cc \
	zisync/kernel/utils/sync_get_handler.h zisync/kernel/utils/sync_get_handler.cc \
	zisync/kernel/worker/refresh_worker.h zisync/kernel/worker/refresh_worker.cc \
	zisync/kernel/router.h zisync/kernel/router.cc \
	zisync/kernel/monitor/monitor.h zisync/kernel/monitor/monitor.cc \
	zisync/kernel/monitor/fs_monitor.h zisync/kernel/monitor/fs_monitor.cc \
	zisync/kernel/sync_const.h \
    zisync/kernel/libevent/hostcheck.c zisync/kernel/libevent/hostcheck.h \
    zisync/kernel/libevent/openssl_hostname_validation.h zisync/kernel/libevent/openssl_hostname_validation.c \
	zisync/kernel/libevent/libevent++.h zisync/kernel/libevent/libevent++.cc \
	zisync/kernel/libevent/libevent_base.h zisync/kernel/libevent/libevent_base.cc \
	zisync/kernel/libevent/report_data_server.h zisync/kernel/libevent/report_data_server.cc \
	zisync/kernel/libevent/ui_event_server.h zisync/kernel/libevent/ui_event_server.cc \
	zisync/kernel/libevent/transfer_server2.h zisync/kernel/libevent/transfer_server2.cc \
	zisync/kernel/libevent/transfer_connection.h zisync/kernel/libevent/transfer_connection.cc \
	zisync/kernel/libevent/transfer_task.h zisync/kernel/libevent/transfer_task.cc \
	zisync/kernel/libevent/http_request.h zisync/kernel/libevent/http_request.cc \
	zisync/kernel/libevent/http_response.h zisync/kernel/libevent/http_response.cc \
	zisync/kernel/libevent/tar_get_task.h zisync/kernel/libevent/tar_get_task.cc \
	zisync/kernel/libevent/tar_put_task.h zisync/kernel/libevent/tar_put_task.cc \
	zisync/kernel/libevent/tar_download_task.h zisync/kernel/libevent/tar_download_task.cc \
	zisync/kernel/libevent/tar_upload_task.h zisync/kernel/libevent/tar_upload_task.cc \
	zisync/kernel/libevent/tar.h \
	zisync/kernel/libevent/libtar++.h zisync/kernel/libevent/libtar++.cc \
	zisync/kernel/libevent/tar_reader.h zisync/kernel/libevent/tar_reader.cc \
	zisync/kernel/libevent/tar_writer.h zisync/kernel/libevent/tar_writer.cc \
	zisync/kernel/libevent/discover.h \
	zisync/kernel/libevent/dht.h zisync/kernel/libevent/dht.c zisync/kernel/libevent/dht_func.c \
	zisync/kernel/libevent/discover_server.h zisync/kernel/libevent/discover_server.cc \
	zisync/kernel/libevent/discover_data_source.h zisync/kernel/libevent/discover_data_source.cc \
	zisync/kernel/monitor/fsevent_report_request_ios.pb.cc \
	zisync/kernel/monitor/MonitorReportEventRequestIOS.h \
	zisync/kernel/worker/report_monitor.h zisync/kernel/worker/report_monitor.cc \
	zisync/kernel/history/history_data_source.h zisync/kernel/history/history_data_source.cc \
	zisync/kernel/history/history_manager.h zisync/kernel/history/history_manager.cc  \
  zisync/kernel/utils/plain_config.h zisync/kernel/utils/plain_config.cc

zisync/kernel/utils/request.h zisync/kernel/utils/response.h : zisync/kernel/proto/kernel.proto
	zisync/kernel/utils/transfer_protobuf.sh zisync/kernel/proto/kernel.proto zisync/kernel/utils/request.h zisync/kernel/utils/response.h

zisync/kernel/proto/kernel.pb.cc zisync/kernel/proto/kernel.pb.h : zisync/kernel/proto/kernel.proto
	$(PROTOC) --cpp_out=./ zisync/kernel/proto/kernel.proto
zisync/kernel/proto/verify.pb.cc zisync/kernel/proto/verify.pb.h : zisync/kernel/proto/verify.proto
	$(PROTOC) --cpp_out=./ zisync/kernel/proto/verify.proto
zisync/kernel/transfer/transfer.pb.cc zisync/kernel/transfer/transfer.pb.h : zisync/kernel/transfer/transfer.proto
	$(PROTOC) --cpp_out=./ zisync/kernel/transfer/transfer.proto

zisync/kernel/monitor/fsevent_report_request_ios.pb.cc zisync/kernel/monitor/fsevent_report_request_ios.pb.h :  zisync/kernel/monitor/fsevent_report_request_ios.proto
	$(PROTOC) --cpp_out=./ zisync/kernel/monitor/fsevent_report_request_ios.proto
libkernel_a_CXXFLAGS =  -I@PROJECT_HEADER_DIR@ -I@THIRD_PART_HEADER_DIR@ -I@PROJECT_DIR@\
						-I@PROJECT_DIR@/src -L@THIRD_PART_LIB_DIR@ 
libkernel_a_CFLAGS =    -I@PROJECT_HEADER_DIR@ -I@THIRD_PART_HEADER_DIR@ \
						-I@PROJECT_DIR@/src
#libkernel_la_LIBADD = @THIRD_PART_LIB_DIR@/libzmq.a @THIRD_PART_LIB_DIR@/libsqlite3.a \
#						@THIRD_PART_LIB_DIR@/libprotobuf.a \
#						@THIRD_PART_LIB_DIR@/libuuid.a @THIRD_PART_LIB_DIR@/libtar.a \
#						@THIRD_PART_LIB_DIR@/libcrypto.a 

ARSCRIPT = arscript


libzskernel.a : libkernel.a  @THIRD_PART_LIB_DIR@/libsqlcipher.a \
	@THIRD_PART_LIB_DIR@/libzmq.a \
	@THIRD_PART_LIB_DIR@/libprotobuf.a \
	@THIRD_PART_LIB_DIR@/libuuid.a @THIRD_PART_LIB_DIR@/libtar.a \
	@THIRD_PART_LIB_DIR@/libcrypto.a \
	@THIRD_PART_LIB_DIR@/libssl.a \
	@THIRD_PART_LIB_DIR@/libuuid.a \
	@THIRD_PART_LIB_DIR@/libevent.a \
	@THIRD_PART_LIB_DIR@/libevent_pthreads.a \
    @THIRD_PART_LIB_DIR@/libevent_openssl.a
	case "${host_os}" in \
		linux*) \
			echo "CREATE $@" > $(ARSCRIPT); \
			for lib in $^; do (echo "ADDLIB $$lib" >> $(ARSCRIPT)); done; \
			echo SAVE >> $(ARSCRIPT); \
			echo END >> $(ARSCRIPT); \
			ar -M < $(ARSCRIPT); \
			rm $(ARSCRIPT); \
		;; \
		*darwin*) \
			libtool -static -o $@ $^ ; \
		;; \
	esac

#libzskernel_a_LDFLAGS = -lcrypto
