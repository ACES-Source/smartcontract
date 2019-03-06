#!/bin/bash

# build Berkeley DB v4.8
echo "=== Building BDB now..."
cd /tmp
# Note: would be nice if 'apt-get source libdbd4.8' (or similar) would work
wget 'http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz'
tar xzf db-4.8.30.NC.tar.gz
cd /tmp/db-4.8.30.NC/build_windows

# CFLAGS="-m32 -mtune=generic" ../dist/configure --enable-cxx --enable-mingw --host=i686-w64-mingw32.static  --disable-replication

# Use it if you want to build for x64
CFLAGS="-mtune=generic" ../dist/configure --enable-cxx --enable-mingw --host=${CROSS}  --disable-replication

# Use it if you want to build for x32
# CFLAGS="-m32 -mtune=generic" ../dist/configure --enable-cxx --enable-mingw --host=${CROSS}  --disable-replication

/bin/sed -i 's/#include <direct.h>//' ../dist/../dbinc/win_db.h
make
make library_build
make install_lib install_include
echo "=== done building DB =="
