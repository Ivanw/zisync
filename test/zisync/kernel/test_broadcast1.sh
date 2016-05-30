#!/bin/sh
dir1=./discover/case1
mkdir -p $dir1
./test_discover -i 1 -p $dir1 -d 8848 -r 8749 -a broadcast -m b1234235 -s broadcast_uuid_2
