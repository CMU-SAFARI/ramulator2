#!/bin/bash

## Build USIMM
cd simulators/usimm-v1.3/src
make clean
make CC=gcc-12 CXX=g++-12 -j
cd ../../..
cp simulators/usimm-v1.3/bin/usimm .

## Build DRAMSim2
cd simulators/DRAMSim2
make clean
make CC=gcc-12 CXX=g++-12 -j
cd ../..
cp simulators/DRAMSim2/DRAMSim ./dramsim2

## Build DRAMSim3
cd simulators/DRAMsim3
rm -rf build
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=gcc-12 -DCMAKE_CXX_COMPILER=g++-12 -DTHERMAL=0 ..
make -j
cd ../../..
cp simulators/DRAMsim3/build/dramsim3main ./dramsim3
cp simulators/DRAMsim3/libdramsim3.so .

## Build ramulatorv1
cd simulators/ramulatorv1
make clean
make CC=gcc-12 CXX=g++-12 -j
cd ../..
cp simulators/ramulatorv1/ramulator ./ramulatorv1

## Build ramulatorv2
cd simulators/ramulatorv2
rm -rf build
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=gcc-12 -DCMAKE_CXX_COMPILER=g++-12 ..
make -j
cd ../../..
cp simulators/ramulatorv2/build/ramulator ./ramulatorv2
