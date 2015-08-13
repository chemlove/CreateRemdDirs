#!/bin/bash

. ../MasterTest.sh

CleanFiles run.000 mremd.opts

cat > mremd.opts <<EOF
DIMENSION   ../Temperatures.dat
DIMENSION   ../Hamiltonians.dat 
DIMENSION   ../AmdDihedral.dat
NSTLIM      500
DT          0.002
NUMEXCHG    100
TEMPERATURE 300.0
TOPOLOGY    ../../full.parm7
MDIN_FILE   ../pme.remd.gamma1.opts
# Only fully archive lowest Hamiltonian
FULLARCHIVE 0
EOF

OPTLINE="-i mremd.opts -b 0 -e 0 -c ../../CRD"
RunTest "M-REMD relative path test."
DoTest mremd.dim.save run.000/remd.dim
DoTest relative.groupfile.save run.000/groupfile
DoTest in.001.save run.000/INPUT/in.001

EndTest