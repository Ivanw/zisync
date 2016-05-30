#bin_PROGRAMS= test_database test_utils
TESTS=test_database 		\
	test_utils 				\
	test_kernel 			\
	test_x509_cert 			\
	test_pkcs12_cert 		\
	test_transfer_server	\
	test_parse_http_header	\
	test_tar_reader 		\
	test_tar_writer 		\
	test_libevent        \
	test_history			\
	test_history_kernel \
  test_remove_remote_file \
  test_commondictionary
noinst_PROGRAMS= test_database 	\
	test_utils 					\
	test_kernel 				\
	test_transfer_proto 		\
	test_discover 				\
	test_data_store				\
	test_daemon 				\
	test_x509_cert				\
	test_transfer_server 		\
	test_pkcs12_cert 			\
	test_create_file 			\
	test_ui_event_server 		\
	test_report_data_server 	\
	test_parse_http_header 		\
	test_tar_reader 			\
	test_tar_writer 			\
	test_async 					\
	test_libevent  			\
	test_history 				\
	test_history_kernel \
  test_remove_remote_file \
  test_commondictionary

# EXTRA_DIST= codestat.h

# AM_CFLAGS = -D_LINUX
AM_CXXFLAGS = -I@PROJECT_HEADER_DIR@ -I@THIRD_PART_HEADER_DIR@ -I@PROJECT_DIR@/src -I@PROJECT_DIR@/test -L/opt/local/lib -D__STDC_FORMAT_MACROS
AM_LDFLAGS = @PROJECT_DIR@/src/libzskernel.a @THIRD_PART_LIB_DIR@/libUnitTest++.a @THIRD_PART_LIB_DIR@/libtar.a -ldl
test_database_SOURCES=  \
	framework.cc \
	database/test_icore.cc \
	database/test_icontent.cc \
	database/test_xsqlite.cc \
	database/test_content_provider.cc \
	database/test_content_resolver.cc 

test_database_LDADD = 
# test_database_LDFLAGS = -lpthread -lglib-2.0 -L/usr/bin $(all_libraries)
# test_database_CXXFLAGS = -I@PROJECT_HEADER_DIR@ -I@THIRD_PART_HEADER_DIR@ \
# 						  -I@PROJECT_DIR@/src
test_database_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_utils_SOURCES = framework.cc \
					 utils/test_zmq.cc \
					 utils/test_fs.cc \
					 utils/test_synclist.cc \
					 utils/test_cipher.cc \
					 utils/test_normalize.cc \
					 utils/test_ignore.cc \
					 utils/test_utils.cc
test_utils_LDADD = 

test_utils_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

#test_transfer_list_LDADD=
#test_transfer_list_SOURCES = test_transfer_list.cc

test_async_LDADD=
test_async_SOURCES= libevent/test_async.cc

test_kernel_SOURCES = test_kernel.cc
test_kernel_LDADD =
test_kernel_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_history_SOURCES = test_history.cc
test_history_LDADD = 
test_history_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_history_kernel_SOURCES = test_history_kernel.cc
test_history_kernel_LDADD = 
test_history_kernel_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_remove_remote_file_SOURCES = test_remove_remote_file.cc
test_remove_remote_file_LDADD = 
test_remove_remote_file_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_commondictionary_SOURCES = test_commondictionary.cc
test_commondictionary_LDADD = 
test_commondictionary_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

#test_transfer_SOURCES =  \
#	transfer/test_task.cc  
#test_transfer_LDADD = 
#test_transfer_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_transfer_proto_SOURCES =  \
	transfer/transfer_test_proto.cc  
test_transfer_proto_LDADD = 
test_transfer_proto_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_create_file_SOURCES = \
	transfer/test_create_file.cc
test_create_file_LDADD =
test_create_file_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_transfer_server_SOURCES = \
	transfer/test_transfer_server.cc \
	test_tool_func.h test_tool_func.cc \
	test_reporter_dox_stdout.h test_reporter_dox_stdout.cc
test_transfer_server_LDADD =
test_transfer_server_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_parse_http_header_SOURCES = \
	libevent/test_parse_http_header.cc
test_parse_http_header_LDADD =
test_parse_http_header_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_tar_reader_SOURCES = libevent/test_tar_reader.cc \
						  test_tool_func.h test_tool_func.cc
test_tar_reader_LDADD =
test_tar_reader_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_tar_writer_SOURCES = libevent/test_tar_writer.cc \
						  test_tool_func.h test_tool_func.cc
test_tar_writer_LDADD =
test_tar_writer_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_discover_SOURCES =  test_discover_server.cc
test_discover_LDADD =  
test_discover_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

#test_idiscover_SOURCES = test_idiscover_server.cc
#test_idiscover_LDADD = -ldl 
#test_idiscover_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_data_store_SOURCES = test_discover_data_store.cc
test_data_store_LDADD = -ldl 
test_data_store_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_ui_event_server_SOURCES = test_ui_event_server.cc
test_ui_event_server_LDADD = -ldl 
test_ui_event_server_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_report_data_server_SOURCES = test_report_data_server.cc
test_report_data_server_LDADD = -ldl
test_report_data_server_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_daemon_SOURCES = daemon/test_daemon.cc
test_daemon_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_x509_cert_SOURCES =  test_x509_cert.cc
test_x509_cert_LDADD =  -ldl
test_x509_cert_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_pkcs12_cert_SOURCES =  test_pkcs12_cert.cc
test_pkcs12_cert_LDADD = -ldl
test_pkcs12_cert_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a

test_libevent_SOURCES =  libevent/test_libevent_main.cc	\
	libevent/test_libevent_base.cc						\
	libevent/test_transfer_server2.cc
test_libevent_LDADD = 
test_libevent_DEPENDENCIES = @PROJECT_DIR@/src/libzskernel.a
