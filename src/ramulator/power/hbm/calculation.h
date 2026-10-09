// Ported from Ayna (CMU-SAFARI/HBM-Power@318d3d1), a derivative of DRAMPower.
// Copyright (c) 2026, SAFARI Research Group at ETH Zurich
// Copyright (c) 2022, Technische Universität Kaiserslautern, Fraunhofer IESE
// BSD 3-Clause License, see LICENSE in this directory.
//
// Sources: standards/hbm2/core_calculation_HBM2.h, standards/hbm3/core_calculation_HBM3.h.
// calcEnergy() takes the memspec and the window stats instead of the DRAM object.

#ifndef RAMULATOR_POWER_HBM_CALCULATION_H
#define RAMULATOR_POWER_HBM_CALCULATION_H

#include <cstddef>
#include <cstdint>

#include "ramulator/power/hbm/energy.h"
#include "ramulator/power/hbm/memspec.h"
#include "ramulator/power/hbm/state.h"

namespace Ramulator::ayna {

class Calculation_HBM2 {
 public:
  // Computes the per-component energy for the window described by `stats`.
  energy_t calcEnergy(const MemSpec& memSpec, const SimulationStats& stats);
};

class Calculation_HBM3 {
 private:
  double E_act(double VDD, double I_theta, double I_1, double t_RAS, uint64_t N_act);
  double E_pre(double VDD, double IBeta, double IDD2_N, double t_RP, uint64_t N_pre);
  double E_BG_pre(std::size_t B, double VDD, double IDD2_N, double T_BG_pre);
  // Read energy: excess current during burst read
  double E_RD(double VDD, double IDD4R, double IDD3N, double t_CK, std::size_t BL, std::size_t DR, uint64_t N_RD);
  // Write energy: excess current during burst write
  double E_WR(double VDD, double IDD4W, double IDD3N, double t_CK, std::size_t BL, std::size_t DR, uint64_t N_WR);
  double E_ref_ab(std::size_t B, double VDD, double IDD5B, double IDD3N16, double tRFC, uint64_t N_REF);

 public:
  // Computes the per-component energy for the window described by `stats`.
  energy_t calcEnergy(const MemSpec& memSpec, const SimulationStats& stats);
};

}  // namespace Ramulator::ayna

#endif  // RAMULATOR_POWER_HBM_CALCULATION_H
