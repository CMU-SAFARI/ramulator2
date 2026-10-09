// Ported from Ayna (CMU-SAFARI/HBM-Power@318d3d1), a derivative of DRAMPower.
// Copyright (c) 2026, SAFARI Research Group at ETH Zurich
// Copyright (c) 2022, Technische Universität Kaiserslautern, Fraunhofer IESE
// BSD 3-Clause License, see LICENSE in this directory.
//
// Energy result types, unchanged except for JSON output. Source: data/energy.h, data/energy.cpp.
// All energies are in pJ.

#ifndef RAMULATOR_POWER_HBM_ENERGY_H
#define RAMULATOR_POWER_HBM_ENERGY_H

#include <cstddef>
#include <vector>

namespace Ramulator::ayna {

struct energy_info_t {
  double E_act = 0.0;
  double E_pre = 0.0;
  double E_bg_act = 0.0;
  double E_bg_pre = 0.0;

  double E_RD = 0.0;
  double E_WR = 0.0;
  double E_RDA = 0.0;
  double E_WRA = 0.0;
  double E_pre_RDA = 0.0;
  double E_pre_WRA = 0.0;

  double E_ref_AB = 0.0;
  double E_ref_PB = 0.0;
  double E_ref_SB = 0.0;
  double E_ref_2B = 0.0;

  // E_pre_RDA and E_pre_WRA are reported but, as upstream, not part of the total.
  double total() const {
    auto total = E_act + E_pre + E_bg_act + E_bg_pre

                 + E_RD + E_WR + E_RDA +
                 E_WRA
                 //+ E_pre_RDA
                 //+ E_pre_WRA

                 + E_ref_AB + E_ref_PB + E_ref_SB + E_ref_2B;

    return total;
  };

  energy_info_t& operator+=(const energy_info_t& other) {
    this->E_act += other.E_act;
    this->E_pre += other.E_pre;
    this->E_bg_act += other.E_bg_act;
    this->E_bg_pre += other.E_bg_pre;

    this->E_RD += other.E_RD;
    this->E_WR += other.E_WR;
    this->E_RDA += other.E_RDA;
    this->E_WRA += other.E_WRA;
    this->E_pre_RDA += other.E_pre_RDA;
    this->E_pre_WRA += other.E_pre_WRA;

    this->E_ref_AB += other.E_ref_AB;
    this->E_ref_PB += other.E_ref_PB;
    this->E_ref_SB += other.E_ref_SB;
    this->E_ref_2B += other.E_ref_2B;

    return *this;
  }
};

struct energy_t {
  std::vector<energy_info_t> bank_energy;

  double E_bg_act_shared = 0.0;
  double E_PDNA = 0.0;
  double E_PDNP = 0.0;
  double E_sref = 0.0;
  double E_dsm = 0.0;
  double E_refab = 0.0;

  energy_t(std::size_t num_banks) : bank_energy(num_banks){};

  double total() const {
    double total = 0.0;

    energy_info_t bank_energy_total;
    for (const auto& bank_e : this->bank_energy) {
      bank_energy_total += bank_e;
    }

    total += bank_energy_total.total() + E_bg_act_shared + E_PDNA + E_PDNP + E_sref + E_dsm + E_refab;

    return total;
  }

  // Sum over banks, with the shared active background folded into E_bg_act.
  energy_info_t total_energy() const {
    energy_info_t total;

    for (const auto& bank_e : this->bank_energy) {
      total += bank_e;
    }

    total.E_bg_act += this->E_bg_act_shared;

    return total;
  }
};

}  // namespace Ramulator::ayna

#endif  // RAMULATOR_POWER_HBM_ENERGY_H
