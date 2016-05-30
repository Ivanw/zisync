#!/bin/bash
#########################################################################
# Author: Pang Hai
# Created Time: Wed Oct  8 15:25:47 2014
# File Name: transfer/test_server.sh
# Description: 
#########################################################################

rm -rf transfer/dest transfer/src

mkdir -p transfer/dest transfer/src transfer/dest/put_file transfer/dest/get_file \
    transfer/dest/error

cd transfer/src
touch file_empty
mkdir -p empty_dir1/empty_dir2
../../test_create_file file_3K 3K
../../test_create_file file_3M 3M
../../test_create_file file_3G 100M
ln -s file_3M file_symbol

mkdir -p dir1/dir2/dir3/
../../test_create_file dir1/dir2/dir3/file_3K 1M
mkdir empty_dir

# create tree and files for transfer list
mkdir tree
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"
../../test_create_file "tree/file${LINENO}" "${LINENO}K"

## create trees for mutil tasks
##put src tree
#mkdir "ptree0"
#mkdir "ptree1"
#mkdir "ptree2"
#mkdir "ptree3"
#cp file_* ptree0
#cp file_* ptree1
#cp file_* ptree2
#cp file_* ptree3
#
##get src tree
#mkdir "gtree0"
#mkdir "gtree1"
#mkdir "gtree2"
#mkdir "gtree3"

cd -
#cd transfer/dest
#
##create trees for mutil tasks
##put dst tree
#mkdir "ptree0"
#mkdir "ptree1"
#mkdir "ptree2"
#mkdir "ptree3"
#
##get src tree
#mkdir "gtree0"
#mkdir "gtree1"
#mkdir "gtree2"
#mkdir "gtree3"

