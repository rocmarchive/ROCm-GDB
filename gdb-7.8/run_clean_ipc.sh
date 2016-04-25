# Just a debug helper script
# Useful only while debugging.
# To kill all the processes and also delete the leftover ipc handlers
# Added since we dont clean up resources on crashes.

pkill -9 gdb
pkill -9 VectorAdd
ipcs
ipcrm -M 5678
ipcrm -M 1234
rm gdb/fifo-agent-w-gdb-r
rm gdb/fifo-gdb-w-agent-r
ipcs
