#!/bin/bash

usage_print()
{
        echo "Usage: " `basename $0` "-r -v"
        echo "Default will only make arm version"
        echo "Support options:"
        echo "  -h  show useage page"
        echo "  -r  release distribution, make both arm and armv7 version"
        echo "  -v  make armv7 version"
}

if [ $# = 0 ]; then
    MAKE_ARM=1
    MAKE_ARMV7=0
else
    while getopts "hrv" OPTION; do
            case "$OPTION" in
                    "h")
                            usage_print
                            exit 0;;
                    "r")
                            MAKE_ARM=1
                            MAKE_ARMV7=1
                            break;;
                    "v")
                            MAKE_ARM=0
                            MAKE_ARMV7=1
                            break;;
                    "*")
                            usage_print
                            exit 0;;
            esac
    done
fi

set -e

source build_android.conf

export CC="$TOOLCHAIN/gcc --sysroot=$SYSROOT"
export CXX="$TOOLCHAIN/g++ --sysroot=$SYSROOT"
export CXXFLAGS="-lstdc++ -lsupc++"
export BUILD_ANDROID_INSTALL=$INSTALL_CMD

echo "arm: $MAKE_ARM"
echo "armv7: $MAKE_ARMV7"



# make arm version
if [ $MAKE_ARM -eq 1 ]; then
export ANDROID_ARCH="arch-arm"
source ./setenv_android.sh

autoreconf -fvi

./configure \
 --host=arm-linux-androideabi \
 --prefix=$PREFIXARM

make clean
make
make install
fi

# make armv7 version
if [ $MAKE_ARMV7 -eq 1 ]; then
export ANDROID_ARCH="arch-armv7"
source ./setenv_android.sh

autoreconf -fvi

./configure \
 --host=armv7-linux-androideabi \
 --prefix=$PREFIXARMV7

make clean
make
make install
fi
