// Ported from Ayna (CMU-SAFARI/HBM-Power@318d3d1).
// Copyright (c) 2026, SAFARI Research Group at ETH Zurich
// BSD 3-Clause License, see LICENSE in this directory.
//
// Ayna's data-rate scaling of the HBM3E rails: array_at() and io_current() of
// case_studies/hbm4_case_study/sweep_hbm4_power.py, with the rails and the "extrapolation" block of
// case_studies/hbm4_case_study/configs/HBM3_6400_power_datapattern.json, followed by the
// per-pseudo-channel split of Ayna's device configurations (every rail lowered by half of IDD2N,
// docs/explanation/idd-derivation.md). At 6400 and 8000 MT/s this gives config/HBM3E_6400MTs and
// config/HBM4_8000MTs. Operation order and rounding follow upstream.

#ifndef RAMULATOR_POWER_HBM_RATE_SCALING_H
#define RAMULATOR_POWER_HBM_RATE_SCALING_H

#include <cstdio>
#include <cstdlib>

#include "ramulator/power/hbm/memspec.h"

namespace Ramulator::ayna::rate_scaling {

constexpr double RATE_BASE = 6.4;      // Gbps/pin: the HBM3E rails below are at 6.4 Gbps
constexpr int STUDIED_MIN_MTS = 4800;  // the range of Ayna's data-rate study
constexpr int STUDIED_MAX_MTS = 8000;

// HBM3E rails at 6.4 Gbps, per pseudo-channel, static data, without the board offset (mA)
constexpr double IDD2N = 141.0;
constexpr double IDD4R = 631.2;
constexpr double IDD4W = 460.0;

// The "extrapolation" block
constexpr double D31 = 0.72;            // IDD3N1 - IDD2N, mA
constexpr double D316 = 2.12;           // IDD3N16 - IDD3N1, mA
constexpr double ACT_INC = 35.6;        // IDD0 - IDD2N at 6.4 Gbps, mA
constexpr double REF_INC = 93.49;       // IDD5B - IDD3N1 at 6.4 Gbps, mA
constexpr double ACT_SLOPE = 9.03e-05;  // relative, per MT/s about 6400
constexpr double REF_SLOPE = 6.75e-05;  // relative, per MT/s about 6400

// Python's round(x, 1), which upstream applies to every rail.
inline double round1(double x) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.1f", x);
  return std::strtod(buf, nullptr);
}

inline bool in_studied_range(int rate_mts) {
  return STUDIED_MIN_MTS <= rate_mts && rate_mts <= STUDIED_MAX_MTS;
}

// Per-pseudo-channel device currents (mA) at rate_mts.
inline void device_currents(int rate_mts, MemSpec::MemPowerSpec& p) {
  const double rate = rate_mts / 1000.0;

  // array_at(rate)
  const double s = rate / RATE_BASE;
  const double idd2n = IDD2N * s;
  const double idd3n1 = idd2n + D31;
  const double idd0 = idd2n + ACT_INC * (1.0 + ACT_SLOPE * (rate - RATE_BASE) * 1000.0);
  const double idd5b = idd3n1 + REF_INC * (1.0 + REF_SLOPE * (rate - RATE_BASE) * 1000.0);
  const double rail_idd0 = round1(idd0);
  const double rail_idd2n = round1(idd2n);
  const double rail_idd3n1 = round1(idd3n1);
  const double rail_idd3n16 = round1(idd3n1 + D316);
  const double rail_idd5b = round1(idd5b);

  // io_current(base, rate)
  const double rail_idd4r = round1(IDD4R * (rate / RATE_BASE));
  const double rail_idd4w = round1(IDD4W * (rate / RATE_BASE));

  // Per-pseudo-channel split
  const double half_idd2n = round1(rail_idd2n / 2);
  p.iDD0 = round1(rail_idd0 - half_idd2n);
  p.iDD2N = round1(rail_idd2n - half_idd2n);
  p.iDD3N1 = round1(rail_idd3n1 - half_idd2n);
  p.iDD3N16 = round1(rail_idd3n16 - half_idd2n);
  p.iDD4R = round1(rail_idd4r - half_idd2n);
  p.iDD4W = round1(rail_idd4w - half_idd2n);
  p.iDD5B = round1(rail_idd5b - half_idd2n);
}

}  // namespace Ramulator::ayna::rate_scaling

#endif  // RAMULATOR_POWER_HBM_RATE_SCALING_H
