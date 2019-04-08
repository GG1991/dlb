#!/bin/bash

# variables detected at configure time
DLB_HOME="/home/guido/GIT/dlb/build"
MPIEXEC="mpirun"
MPIEXEC_EXPORT_FLAG="-x"

DLB=1

APP="./mpi_omp_pils"
ARGS="/dev/null 1 50 300"

if [[ $DLB == 1 ]] ; then
    PRELOAD="$DLB_HOME/lib/libdlb.so"
    export DLB_ARGS+=" --lewi"
else
    export DLB_ARGS+=" --no-lewi"
fi

export OMP_NUM_THREADS=1

time $MPIEXEC -np 2 $MPIEXEC_EXPORT_FLAG LD_PRELOAD=$PRELOAD hpcrun -t $APP $ARGS
