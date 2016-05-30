#!/bin/bash
#########################################################################
# Author: Pang Hai
# Created Time: Wed Aug 27 12:49:34 2014
# File Name: test.sh
# Description: 
#########################################################################

rm transfer/src transfer/dest -rf
if [ ! -d transfer/src ]; then
    mkdir transfer/src
    mkdir transfer/src/1/2/3/4/5 -p
    mkdir transfer/src/1/empty_dir
    if [ -c /dev/zero ]; then
        #dir 1
#        dd if=/dev/zero of=transfer/src/1/file1 bs=1K count=512
#        dd if=/dev/zero of=transfer/src/1/file2 bs=1K count=1024
#        dd if=/dev/zero of=transfer/src/1/file3 bs=1M count=3
        ./test_create_file transfer/src/1/file1 512K
        ./test_create_file transfer/src/1/file2 1M
        ./test_create_file transfer/src/1/file3 3M
        cp transfer/src/1/file1 transfer/src/1/2
        cp transfer/src/1/file2 transfer/src/1/2/3/4/5

        #file file3
        cp transfer/src/1/file3 transfer/src
        #empty_dir
        cp transfer/src/1/empty_dir transfer/src -r
    fi
    echo "lajdflsdjflsdjflsjflj" > transfer/src/file
fi

if [ ! -d transfer/dest ]; then
    mkdir transfer/dest
fi

if [ ! -d transfer/dest/expected ]; then
    mkdir transfer/dest/expected
fi

if [ ! -d transfer/dest/expected/get_send_dir ]; then
    mkdir transfer/dest/expected/get_send_dir
fi

if [ ! -d transfer/dest/tmp_dir ]; then
    mkdir transfer/dest/tmp_dir
fi

if [ ! -d transfer/dest/expected/tmp_dir ]; then
    mkdir transfer/dest/expected/tmp_dir
fi

if [ ! -d transfer/dest/expected/server_tmp_dir ]; then
    mkdir transfer/dest/expected/server_tmp_dir
fi

if [ ! -d transfer/dest/server_tmp_dir ]; then
    mkdir transfer/dest/server_tmp_dir
fi

if [ ! -d transfer/dest/server_get_root ]; then
    mkdir transfer/dest/server_get_root
fi

if [ ! -d transfer/dest/test_error ]; then
    mkdir transfer/dest/test_error
fi

#create the expected file corresponding to the transfer/src file

cd transfer/src
files=$(ls)

tree_uuid="client"
for file in $files; do
    #expected put file
    echo -ne "PUT tar HTTP/1.1\r\nZiSync-Tree-Uuid:${tree_uuid}\r\n\r\n" > \
        "../dest/expected/${file}_expected"
    tar cvf - "${file}" >> "../dest/expected/${file}_expected" --format=gnu --record-size=512

    echo -ne "HTTP/1.1 200 OK\r\n\r\n" > "${file}_get"
    tar cvf - ${file} --format=gnu >> "${file}_get" #get transfer/src file
    cp "${file}" "../dest/expected/tmp_dir/" -a

    echo -ne "GET tar HTTP/1.1\r\nZiSync-Tree-Uuid:${tree_uuid}\r\n\r\n" > \
        "../dest/expected/get_send_dir/${file}_expected" 
    ../../test_transfer_proto "${file}" "../dest/expected/get_send_dir/${file}_expected"

    cp "${file}" "../dest/expected/server_tmp_dir/" -a
done

#test more files
echo -ne "PUT tar HTTP/1.1\r\nZiSync-Tree-Uuid:${tree_uuid}\r\n\r\n" > \
    "../dest/expected/1_more_expected"
tar cvf - 1 file file3 empty_dir >> "../dest/expected/1_more_expected" --format=gnu --record-size=512


echo -ne "HTTP/1.1 200 OK\r\n\r\n" > "put_response"

cd ..
