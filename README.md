# ROCm-GDB - Deprecated Project
## This ROCm Debugger is a Deprecated project.
As of 2018, this is a deprecated software project. The ROCm software team is working on a new GDB-based debugger that works with the ROCr Debug Agent to support debugging GPU kernels.

# Overview
The ROCm-GDB repository includes the source code for ROCm-GDB. ROCm-GDB is a modified version of GDB 7.11 that supports debugging GPU kernels on Radeon Open Compute platforms (ROCm).

# Package Contents
The ROCm-GDB repository includes
* A modified version of gdb-7.11 to support GPU debugging. Note the main ROCm specific files are located in *gdb-7.11/gdb* with the *rocm-** prefix.
* The ROCm debug facilities library located in *amd/HwDbgFacilities/*. This library provides symbol processing for GPU kernels.

# Build Steps
1. Clone the ROCm-GDB repository
  * `git clone https://github.com/RadeonOpenCompute/ROCm-GDB.git`
2. The gdb build has been modified with new files and configure settings to enable GPU debugging. The scripts below should be run to compile gdb.
The *run_configure_rocm.sh* script calls the GNU autotools configure with additional parameters. The *run_configure_rocm.sh* script will create the build directory to build the gdb executable in a out of source manner
  * `./run_configure_rocm.sh debug`
3. The `run_configure_rocm.sh` script also generates the *run_make_rocm.sh* which sets environment variables for the *Make* step
  * `./run_make_rocm.sh`

# Running ROCm-GDB
The `run_make_rocm.sh` script builds the gdb executable which will be located in build/gdb/

To run the ROCm debugger, you'd also need to get the [ROCm GPU Debug SDK](https://github.com/RadeonOpenCompute/ROCm-GPUDebugSDK).

Before running the rocm debugger, the *LD_LIBRARY_PATH* should include paths to
* The ROCm GPU Debug Agent library built in the ROCm GPU Debug SDK (located in *gpudebugsdk/lib/x86_64*)
* The ROCm GPU Debugging library binary shippped with the ROCm GPU Debug SDK (located in *gpudebugsdk/lib/x86_64*)
* Before running ROCm-GDB, please update your .gdbinit file  with text in *gpudebugsdk/src/HSADebugAgent/gdbinit*. The rocmConfigure function in the ~/.gdbinit sets up gdb internals for supporting GPU kernel debug.
* The gdb executable should be run from within the *rocm-gdb-local* script. The ROCm runtime requires certain environment variables to enable kernel debugging and this is set up by the *rocm-gdb-local* script.
```
./rocm-gdb-local < sample application>
```
* [A brief tutorial on how to debug GPU applications using ROCm-GDB](https://github.com/RadeonOpenCompute/ROCm-Debugger/blob/master/TUTORIAL.md)

