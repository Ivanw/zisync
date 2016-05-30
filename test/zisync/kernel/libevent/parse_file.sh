#!/bin/bash

longname="1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"
mkdir libevent/tmp_dir libevent/files
rm libevent/tmp_dir/* -rf
cd libevent/files
touch empty_file
../../test_create_file file1 3M
../../test_create_file ${longname} 3M
../../test_create_file file_4G 10M
