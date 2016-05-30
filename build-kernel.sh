#!/bin/sh

#  Automatic build script for libssl and libcrypto 
#  for iPhoneOS and iPhoneSimulator
#
#  Created by Felix Schulze on 16.12.10.
#  Copyright 2010 Felix Schulze. All rights reserved.
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#
###########################################################################
#  Change values here													  #

CURRENTPATH=`pwd`
ARCHS="armv7 armv7s arm64"
DEVELOPER=`xcode-select -print-path`
sdkdir=$DEVELOPER/Platforms/iPhoneOS.platform/Developer/SDKs
SDKVERSION="8.1"														  #
if [ ! -d $sdkdir/iPhoneOS8.1.sdk   ] ; then
    SDKVERSION="7.1";
fi

while [[ $# -ge 1  ]]
do
    key="$1"
    shift
    case $key in
        verbose)
            verbose="yes"
            ;;
        8.1)
            SDKVERSION="8.1"
            ;;
        7.1)
            SDKVERSION="7.1"
            ;;
        *)
            # unknown option
            ;;
    esac
done
VERSION="1.0.1i"													      #
echo will link: $sdkdir/iPhoneOS$SDKVERSION.sdk
#																		  #
###########################################################################
#																		  #
# Don't change anything under this line!								  #
#																		  #
###########################################################################


uniqueName=`pwd`
uniqueName=`basename $uniqueName`
theIncDir="${CURRENTPATH}/build/include"
theBinDir="${CURRENTPATH}/build/$uniqueName/bin"
theLibDir="${CURRENTPATH}/build/lib"
mkdir -p $theBinDir
mkdir -p $theLibDir 
mkdir -p $theIncDir

if [ -e  $theLibDir/kernels  ] ; then
    rm -rf $theLibDir/kernels
fi

if [  -e src/kernels  ] ; then
    #sufix=`date "+%Y_%m_%d_%H_%M_%S"`
    #mv src/kernels src/kernels-$sufix
    rm -rf src/kernels
fi
mkdir -p src/kernels

for ARCH in ${ARCHS}
do
    if [[ "${ARCH}" == "i386" || "${ARCH}" == "x86_64" ]];
    then
        PLATFORM="iPhoneSimulator"
    else
        #	sed -ie "s!static volatile sig_atomic_t intr_signal;!static volatile intr_signal;!" "crypto/ui/ui_openssl.c"
        PLATFORM="iPhoneOS"
    fi

    DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
    SDKROOT="${DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"
    #only for openssl
    #	export CROSS_TOP="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
    #	export CROSS_SDK="${PLATFORM}${SDKVERSION}.sdk"
    #	export BUILD_TOOLS="${DEVELOPER}"

    echo "Building `pwd` for ${PLATFORM} ${SDKVERSION} ${ARCH}"
    echo "Please stand by..."

    export CC="${BUILD_TOOLS}/usr/bin/gcc"
    export CXX="${BUILD_TOOLS}/usr/bin/g++"
    export CPPFLAGS="-arch $ARCH"
    export LDFLAGS="-arch $ARCH -framework CoreFoundation"
    export CXXFLAGS="-I$SDKROOT/System/Library/Frameworks"
    PREFIX=$theBinDir/${PLATFORM}${SDKVERSION}-${ARCH}.sdk
    mkdir -p "$theBinDir/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"
    if [[ "${ARCH}" == "i386" || "${ARCH}" == "x86_64" ]]
    then
        ARCHFLAG=" --host=$ARCH-apple-darwin "
    elif [[ "${ARCH}" == "armv7" || "${ARCH}" == "armv7s"  ]]
    then
        ARCHFLAG=" --host=$ARCH-apple-darwin "
    else
        ARCHFLAG=" --host=aarch64-apple-darwin "
    fi

    if [[ "${ARCH}" != "i386" && "${ARCH}" != "x86_64" ]]
    then
        export CPP="$CC -E"
        export LD="/usr/bin/ld"
        #export PKG_CONFIG_PATH="$SDKROOT/usr/lib/pkgconfig":"$PREFIX/lib/pkgconfig"
        export CPPFLAGS="$CPPFLAGS -isysroot $SDKROOT"
        export CFLAGS="$CPPFLAGS -miphoneos-version-min=7.0 -std=c99 -pipe -no-cpp-precomp"
        export CXXFLAGS="$CPPFLAGS -miphoneos-version-min=7.0 -I$SDKROOT/usr/include/c++/4.2.1/$ARCH -pipe -no-cpp-precomp"
        export LDFLAGS="$LDFLAGS -isysroot $SDKROOT"
    fi

    LOG="$theBinDir/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/build-openssl-${VERSION}.log"

    #    set +e
    ./configure $ARCHFLAG --prefix=$PREFIX --enable-shared=no --enable-debug > "${LOG}" 2>&1

    if [ $? != 0 ];
    then 
        echo "Problem while configure - Please check ${LOG}"
        exit 1
    fi
    # add -isysroot to CC=
    #sed -ie "s!^CFLAG=!CFLAG=-isysroot ${CROSS_TOP}/SDKs/${CROSS_SDK} -miphoneos-version-min=7.0 !" "Makefile"

    make clean >> "${LOG}" 2>&1
    if [ -n $verbose ];
    then
        make
    else
        make >> "${LOG}" 2>&1
    fi

    if [ $? != 0 ];
    then 
        echo "Problem while make - Please check ${LOG}"
        exit 1
    fi


    if [ ! -e  $theLibDir/kernels  ] ; then
        mkdir $theLibDir/kernels
    fi
    cp src/libkernel.a src/kernels/
    mv src/kernels/libkernel.a  src/kernels/libkernel-${ARCH}.a

    #    set -e
    make install >> "${LOG}" 2>&1
done

cp -R ./include/ $theIncDir
cp -R src/ $theIncDir
find $theIncDir ! -iname "*.h" -and ! -type d -and -exec rm {} \; 
find $theIncDir -type d -and -empty -and -exec rm -rf {} \;
echo "Build library..."

theLibsToAdd=""
for  onelib in `find src/kernels/ -iname *.a`
do
    filename=`basename $onelib`
    theLibsToAdd="$theLibsToAdd $onelib"
done


libName2Use=`pwd`
libName2Use=`basename $libName2Use`

lipo -create $theLibsToAdd -output $theLibDir/$libName2Use.a
#echo "Cleaning up..."
echo "Done."
