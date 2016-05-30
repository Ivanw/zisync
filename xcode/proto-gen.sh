#!/bin/bash
../src/zisync/kernel/utils/transfer_protobuf.sh ../src/zisync/kernel/proto/kernel.proto ../src/zisync/kernel/utils/request.h ../src/zisync/kernel/utils/response.h
cd ../src/zisync/kernel/proto/ 
protoc --cpp_out=./ kernel.proto
cd -
cd ../src/zisync/kernel/transfer/ 
protoc --cpp_out=./ transfer.proto
cd -
cd ../src/zisync/kernel/monitor/
protoc --cpp_out=./ fsevent_report_request_ios.proto
cd -
