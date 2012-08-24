#!/bin/bash

# Node count
export NODES="2"

# Time limit
export TIMEM="150"
export TIME=$(( ( TIMEM - 3 ) * 60 ));

# Build settings env vars
source mcw.conf

# Run the computational job
rm -f cleaned
ulimit -c unlimited
time mpirun -x MCW_DB_PATH -x MCW_DB_PREFIX -x MCW_DB_COUNT -x MCW_DB_FILES -x MCW_Q_FILE -x MCW_Q_WUM -x MCW_S_TIMELIMIT -x MCW_S_EXE -x MCW_S_LINE -x MCW_S_DUP2 -x MCW_S_DUP2 ./mcw
