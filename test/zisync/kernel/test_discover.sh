#!/bin/sh
dir1=./discover/case1
mkdir -p $dir1
./test_discover -i 3 -p $dir1 -d 9870 -r 8749 -a lunabox -m 123456789 -s uuid_1 &
dir2=./discover/case2
mkdir -p $dir2
./test_discover -i 3 -p $dir2 -d 9871 -r 8750 -a lunabox -m 123456788 -s uuid_2 &
dir3=./discover/case3
mkdir -p $dir3
./test_discover -i 3 -p $dir3 -d 9878 -r 8751 -a lunabox -m 123456787 -s uuid_3 
