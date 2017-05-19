#!/bin/bash

# High level logic of the ROCm-gdb build system
#
# The run_configure_rocm script calls GDB's autotools configure system
# with additional parameters to enable hsail debugging.
#
# The run_configure_rocm also generates a run_make_rocm.sh which
# includes the proper environment to call  gdb's underlying make

if (($# != 1))
then
    IPOPTION='debug'
else
    IPOPTION=$1
fi

if [[ $IPOPTION == '-h' ]]
then
	echo "The ROCm-GDB configure script takes only one argument"
	echo "smash: Clean up config cache"
	echo "debug: Use debug version of Debug Facilities in (amd/HwDbgFacilities) and build gdb with debug symbols"
	echo "release: Use release version of Debug Facilities in (amd/HwDbgFacilities) and build gdb for release"
	exit 
fi

# Smash the old build otherwise it complains about config.cache
if [[ $IPOPTION == 'smash' ]]
then
	bash run_clean_cache.sh
        exit $?
fi

hwdbgfct_opt=""

# Takes the name of the debug facilities library name as argument
# Generates a "run_make_rocm.sh" script that runs the Makefile with the env var that
# specifies the debug facilities library
#
# For internal users, they should rename amd-gdb to gdb if they are debugging the executable itself.
# This is because GDB detects the inferior
# filename and changes the command prompt to (top-gdb) when it detects that itself is being debugged.
# Thats why the original  name is important
GenerateMakeScript()
{
	touch run_make_rocm.sh
	echo "# ROCM-GDB Makefile wrapper script" > run_make_rocm.sh
	echo "# This file Should not be checked in" >> run_make_rocm.sh
	echo "# Do not edit manually, will be overwritten when you call run_configure_rocm.sh" >> run_make_rocm.sh
	echo "# " >> run_make_rocm.sh
	echo "# Call HwDbgFacilities make" >> run_make_rocm.sh
	echo "cd ../amd/HwDbgFacilities/" >> run_make_rocm.sh
	echo "make$hwdbgfct_opt" >> run_make_rocm.sh
	echo "MAKE_RESULT=\$?" >> run_make_rocm.sh
	echo "if [ "\$MAKE_RESULT" != "0" ];" >> run_make_rocm.sh
	echo "then" >> run_make_rocm.sh
	echo "    echo \"Build of libAMDHwDbgFacilities-x64.so failed\"" >> run_make_rocm.sh
	echo "    exit \$MAKE_RESULT" >> run_make_rocm.sh
	echo "fi" >> run_make_rocm.sh
	echo "cd -" >> run_make_rocm.sh
	echo "# Set the name of the facilities library we will use" >> run_make_rocm.sh
	echo "export AMD_DEBUG_FACILITIES='$1'" >> run_make_rocm.sh
	echo "# Call GDB's make" >> run_make_rocm.sh
	echo "make" >> run_make_rocm.sh
	echo "MAKE_RESULT=\$?" >> run_make_rocm.sh
	echo "if [ \"\$MAKE_RESULT\" != \"0\" ];" >> run_make_rocm.sh
	echo "then" >> run_make_rocm.sh
	echo "    echo \"Build of rocm-gdb failed\"" >> run_make_rocm.sh
	echo "    exit \$MAKE_RESULT" >> run_make_rocm.sh
	echo "fi" >> run_make_rocm.sh
	echo "# Rename the GDB executable so the ROCm-gdb script can run it" >> run_make_rocm.sh
	echo "mv gdb/gdb gdb/amd-gdb" >> run_make_rocm.sh
	echo "# Copy wrapper scripts to build directory from source directory" >> run_make_rocm.sh
	echo "cp ../gdb/rocm-gdb gdb/rocm-gdb" >> run_make_rocm.sh
	echo "cp ../gdb/rocm-gdb-local gdb/rocm-gdb-local" >> run_make_rocm.sh
	chmod +x run_make_rocm.sh
}


mkdir build_$IPOPTION
cd build_$IPOPTION
# Build Option 1
# With debug symbols of GDB and debug version of Debug facilities
if [[ $IPOPTION == 'debug' ]]
then
../configure --enable-rocm --with-python \
 CFLAGS=' -g  -I../../amd/HwDbgFacilities/include -I../../amd/include -fPIC '\
 LDFLAGS=' -L../../lib/x86_64 -pthread'
 hwdbgfct_opt=" -e HSAIL_build=debug"
 GenerateMakeScript -lAMDHwDbgFacilities-x64-d
fi

# Build Option 2
# With release version of GDB and release version of debug facilities
if [[ $IPOPTION == 'release' ]]
then
../configure --enable-rocm --with-python \
 CFLAGS=' -I../../amd/HwDbgFacilities/include -I../../amd/include -fPIC '\
 LDFLAGS=' -L../../lib/x86_64 -pthread'
 GenerateMakeScript -lAMDHwDbgFacilities-x64
fi

cd ..

