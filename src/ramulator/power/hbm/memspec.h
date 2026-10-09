// Ported from Ayna (CMU-SAFARI/HBM-Power@318d3d1), a derivative of DRAMPower.
// Copyright (c) 2026, SAFARI Research Group at ETH Zurich
// Copyright (c) 2022, Technische Universität Kaiserslautern, Fraunhofer IESE
// BSD 3-Clause License, see LICENSE in this directory.
//
// The MemSpecHBM2 / MemSpecHBM3 fields that the HBM handlers and formulas read, with their
// upstream names and types. Sources: memspec/MemSpecHBM2.{h,cpp}, memspec/MemSpecHBM3.{h,cpp}.
//
// The Ayna plugin fills this from Ramulator's DRAMSpec (timings and organization) and from its
// `power` config (the electrical characterization). Timings are in controller ticks and tCK is
// the tick period in ps, so every formula applies unchanged.

#ifndef RAMULATOR_POWER_HBM_MEMSPEC_H
#define RAMULATOR_POWER_HBM_MEMSPEC_H

#include <cstdint>
#include <vector>

#include "ramulator/power/hbm/data_pattern_model.h"

namespace Ramulator::ayna {

struct MemSpec {
  struct MemTimingSpec {
    double tCK;  // clock period in ps
    uint64_t tRAS;
    uint64_t tRL;  // CAS latency (nCL)
    uint64_t tWL;
    uint64_t tWR;
    uint64_t tRP;
    uint64_t tRFC;  // all-bank refresh cycle time (cycles); 0 = refresh energy not modeled
    uint64_t tBurst;
  };

  struct MemPowerSpec {
    double vDD;
    double vDDQ;  // I/O supply (HBM3/HBM4 split rail); applied to the DQ share of read energy

    double iDD0;
    double iDD2N;
    double iDD3N1;   // active standby, 1 bank open
    double iDD3N16;  // active standby, all banks open
    double iDD4R;
    double iDD4W;
    double iDD5B = 0.0;  // all-bank refresh burst current; 0 = refresh energy not modeled

    std::vector<double> bankgroup_scaling_factors;  // to account for variation between bankgroups
    std::vector<double> bank_scaling_factors;       // to account for variation between banks
    std::vector<double> bankgroup_write_scaling_factors;
    std::vector<double> bank_write_scaling_factors;
  };

  struct BankWiseParams {
    double bwPowerFactRho;
  };

  uint64_t numberOfBanks;  // per bank group
  uint64_t burstLength;
  uint64_t dataRate;
  uint64_t banksPerPseudoChannel;

  MemTimingSpec memTimingSpec;
  MemPowerSpec memPowerSpec;
  BankWiseParams bwParams;
  DataPatternModel dataPattern;

  uint64_t prechargeOffsetRD;
  uint64_t prechargeOffsetWR;

  // MemSpecHBM2/MemSpecHBM3 constructor derivations, unchanged.
  void derive() {
    memTimingSpec.tBurst = burstLength / dataRate;
    bwParams.bwPowerFactRho = 1.0;
    prechargeOffsetRD = memTimingSpec.tRL + memTimingSpec.tBurst;
    prechargeOffsetWR = memTimingSpec.tBurst + memTimingSpec.tWL + memTimingSpec.tWR;
  }
};

}  // namespace Ramulator::ayna

#endif  // RAMULATOR_POWER_HBM_MEMSPEC_H
