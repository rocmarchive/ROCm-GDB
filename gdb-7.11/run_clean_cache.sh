make distclean

rm intl/config.cache
rm gdb/config.cache
rm libiberty/config.cache
rm bfd/config.cache
rm opcodes/config.cache
rm libdecnumber/config.cache

rm readline/config.cache
rm intl/config.cache
rm sim/config.cache
rm etc/config.cache

rm gdb/build-gnulib/config.cache


rm gdb/gdbserver/config.cache

cd ./amd/HwDbgFacilities/
make clean
