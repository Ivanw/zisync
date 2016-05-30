#!/bin/bash
  cd ../src/zisync/kernel/proto/
  protoc --cpp_out=. kernel.proto 
  protoc --cpp_out=. verify.proto
  cd -
  cd ../src/zisync/kernel/transfer/
  protoc --cpp_out=. transfer.proto 
  cd -
