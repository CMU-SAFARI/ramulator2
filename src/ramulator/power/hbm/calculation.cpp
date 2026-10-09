// Ported from Ayna (CMU-SAFARI/HBM-Power@318d3d1), a derivative of DRAMPower.
// Copyright (c) 2026, SAFARI Research Group at ETH Zurich
// Copyright (c) 2022, Technische Universität Kaiserslautern, Fraunhofer IESE
// BSD 3-Clause License, see LICENSE in this directory.
//
// The HBM2 and HBM3 energy formulas, unchanged. Sources: standards/hbm2/core_calculation_HBM2.cpp,
// standards/hbm3/core_calculation_HBM3.cpp. The only edits: calcEnergy() takes the memspec and the
// window stats as arguments (upstream reads them from the DRAM object), and `dram.memSpec.` reads
// `memSpec.`. HBM4 runs on the HBM3 formulas, as in Ayna.

#include "ramulator/power/hbm/calculation.h"

#include <cassert>

namespace Ramulator::ayna {


    energy_t Calculation_HBM2::calcEnergy(const MemSpec &memSpec, const SimulationStats &stats) {

        double t_CK = memSpec.memTimingSpec.tCK;
        auto t_RAS = memSpec.memTimingSpec.tRAS * t_CK;
        auto t_RP = memSpec.memTimingSpec.tRP * t_CK;
        auto t_RFC = memSpec.memTimingSpec.tRFC * t_CK;

        auto rho = memSpec.bwParams.bwPowerFactRho;
        auto BL = memSpec.burstLength;
        auto DR = memSpec.dataRate;
        auto B = memSpec.banksPerPseudoChannel;

        auto VDD = memSpec.memPowerSpec.vDD;            // V
        // IDD values are specified in mA; convert to A so that
        // V * A * ps = pJ (picojoules)
        auto IDD0    = memSpec.memPowerSpec.iDD0    * 1e-3;
        auto IDD2N   = memSpec.memPowerSpec.iDD2N   * 1e-3;
        auto IDD3N1  = memSpec.memPowerSpec.iDD3N1  * 1e-3;
        auto IDD3N16 = memSpec.memPowerSpec.iDD3N16 * 1e-3;
        auto IDD5B   = memSpec.memPowerSpec.iDD5B   * 1e-3;   // 0 => refresh energy off
        // Read/write currents: optionally replaced by the data-pattern model, which
        // derives an effective IDD4R/IDD4W (mA) from the per-bus activity knobs. When
        // the model is disabled, these are the unchanged config IDD4R/IDD4W.
        double iDD4R_mA = memSpec.memPowerSpec.iDD4R;
        double iDD4W_mA = memSpec.memPowerSpec.iDD4W;
        const auto &dp = memSpec.dataPattern;
        if (dp.enabled) {
            const double idd3n1_mA = memSpec.memPowerSpec.iDD3N1;
            iDD4R_mA = dp.effective_current_mA(iDD4R_mA, idd3n1_mA);
            if (dp.apply_to_writes)
                iDD4W_mA = dp.effective_current_mA(iDD4W_mA, idd3n1_mA);
        }
        auto IDD4R   = iDD4R_mA * 1e-3;
        auto IDD4W   = iDD4W_mA * 1e-3;

        // For HBM2 IBeta = IDD2N (no explicit precharge operating current)
        auto IBeta = IDD2N;

        // Derived currents
        // I_rho: bank-wise weighted active standby current
        auto I_rho = rho * (IDD3N1 - IDD2N) + IDD2N;

        // I_theta: activation current derived from IDD0 cycle measurement
        auto I_theta = (IDD0 * (t_RP + t_RAS) - IBeta * t_RP) / t_RAS;

        // I_1: single-bank active standby current distributed across all banks
        auto I_1 = (1.0 / B) * (IDD3N1 + (B - 1) * I_rho);

        assert(I_rho >= 0 && "I_rho must be non-negative");
        assert(I_theta >= 0 && "I_theta must be non-negative");
        assert(I_1 >= 0 && "I_1 must be non-negative");

        energy_t energy(memSpec.banksPerPseudoChannel);

        // Per-additional-active-bank current increment: going from one active bank
        // (IDD3N1) to all B active (IDD3N16) raises active-standby current by
        // (IDD3N16 - IDD3N1) spread over the (B-1) extra banks.
        const double delta_bg_act = (B > 1) ? (IDD3N16 - IDD3N1) / (B - 1) : 0.0;

        // Single pseudo-channel: rank index 0
        const auto &bgFactors  = memSpec.memPowerSpec.bankgroup_scaling_factors;
        const auto &bankFactors = memSpec.memPowerSpec.bank_scaling_factors;
        // Write factors fall back to the read factors when not given.
        const auto &bgWriteFactors = memSpec.memPowerSpec.bankgroup_write_scaling_factors.empty()
            ? bgFactors : memSpec.memPowerSpec.bankgroup_write_scaling_factors;
        const auto &bankWriteFactors = memSpec.memPowerSpec.bank_write_scaling_factors.empty()
            ? bankFactors : memSpec.memPowerSpec.bank_write_scaling_factors;
        const std::size_t banksPerGroup = memSpec.numberOfBanks; // banks per bank-group
        for (std::size_t b = 0; b < B; ++b) {
            const auto &bank = stats.bank[b];

            // Systematic structural variation: scale this bank's read/write current by
            // bankgroup_factor[b / banksPerGroup] * bank_factor[b % banksPerGroup].
            // Empty factor vectors => uniform (factor 1.0), preserving prior behaviour.
            double varFactor = 1.0;
            if (!bgFactors.empty())
                varFactor *= bgFactors[(b / banksPerGroup) % bgFactors.size()];
            if (!bankFactors.empty())
                varFactor *= bankFactors[(b % banksPerGroup) % bankFactors.size()];
            double writeFactor = 1.0;
            if (!bgWriteFactors.empty())
                writeFactor *= bgWriteFactors[(b / banksPerGroup) % bgWriteFactors.size()];
            if (!bankWriteFactors.empty())
                writeFactor *= bankWriteFactors[(b % banksPerGroup) % bankWriteFactors.size()];
            const double IDD4R_b = IDD4R * varFactor;
            const double IDD4W_b = IDD4W * writeFactor;

            // Time this bank spent active (precharged-bank time handled by E_bg_pre below).
            const double T_bank_act = bank.cycles.activeTime() * t_CK;
            // Burst window per access = (BL / DR) data-rate cycles, in ps.
            const double t_burst = (double(BL) / DR) * t_CK;

            // Activation energy: excess activation current (I_theta) above the
            // distributed single-bank active standby (I_1), over t_RAS, per ACT.
            energy.bank_energy[b].E_act +=
                VDD * (I_theta - I_1) * t_RAS * bank.counter.act;

            // Precharge energy: excess precharge current (IBeta) above precharged
            // standby (IDD2N), over t_RP, per explicit PRE.
            energy.bank_energy[b].E_pre +=
                VDD * (IBeta - IDD2N) * t_RP * bank.counter.pre;

            // Active background energy, per bank: each active bank draws delta_bg_act
            // over its active time. Combined with the shared base term below, the first
            // active bank totals IDD3N1 and all B active total IDD3N16.
            energy.bank_energy[b].E_bg_act +=
                VDD * delta_bg_act * T_bank_act;

            // Precharged background energy: precharged standby current (IDD2N) over the
            // rank's precharged time, shared evenly across the B banks (1/B).
            energy.bank_energy[b].E_bg_pre +=
                (1.0 / B) * VDD * IDD2N * (stats.rank_total[0].cycles.pre * t_CK);

            // Read energy: excess read current (IDD4R) above active standby (IDD3N1),
            // over the burst window, per read burst.
            energy.bank_energy[b].E_RD +=
                VDD * (IDD4R_b - IDD3N1) * t_burst * bank.counter.reads;

            // Write energy: excess write current (IDD4W) above active standby (IDD3N1),
            // over the burst window, per write burst.
            energy.bank_energy[b].E_WR +=
                VDD * (IDD4W_b - IDD3N1) * t_burst * bank.counter.writes;

            // Read-with-autoprecharge: read-burst component (precharge component below).
            energy.bank_energy[b].E_RDA +=
                VDD * (IDD4R_b - IDD3N1) * t_burst * bank.counter.readAuto;

            // Write-with-autoprecharge: write-burst component (precharge component below).
            energy.bank_energy[b].E_WRA +=
                VDD * (IDD4W_b - IDD3N1) * t_burst * bank.counter.writeAuto;

            // Precharge component of read-with-autoprecharge (one PRE per RDA).
            energy.bank_energy[b].E_pre_RDA +=
                VDD * (IBeta - IDD2N) * t_RP * bank.counter.readAuto;

            // Precharge component of write-with-autoprecharge (one PRE per WRA).
            energy.bank_energy[b].E_pre_WRA +=
                VDD * (IBeta - IDD2N) * t_RP * bank.counter.writeAuto;

            // All-bank refresh: every bank is held active for tRFC per REFA, so the
            // background terms already charge IDD3N16 (all banks open) over tRFC. The
            // refresh term adds the rest of IDD5B, shared evenly across the B banks, so a
            // back-to-back REFA loop draws exactly IDD5B.
            if (IDD5B > 0.0 && t_RFC > 0.0)
                energy.bank_energy[b].E_ref_AB +=
                    (1.0 / B) * VDD * (IDD5B - IDD3N16) * t_RFC * bank.counter.refAllBank;
        }

        // Shared base active-standby energy: (IDD3N1 - delta_bg_act) over the time the
        // rank has >=1 bank active. The remaining delta_bg_act for the first active bank
        // is supplied by that bank's per-bank term above, so one active bank totals
        // IDD3N1 (and the IDD2N floor is carried through active time, not dropped).
        energy.E_bg_act_shared +=
            VDD * (IDD3N1 - delta_bg_act) * (stats.rank_total[0].cycles.act * t_CK);

        return energy;
    }



    double Calculation_HBM3::E_BG_pre(std::size_t B, double VDD, double IDD2_N, double T_BG_pre) {
        return (1.0 / B) * VDD * IDD2_N * T_BG_pre;
    };

    double Calculation_HBM3::E_pre(double VDD, double IBeta, double IDD2_N, double t_RP, uint64_t N_pre) {
        return VDD * (IBeta - IDD2_N) * t_RP * N_pre;
    }

    double Calculation_HBM3::E_act(double VDD, double I_theta, double I_1, double t_RAS, uint64_t N_act) {
        return VDD * (I_theta - I_1) * t_RAS * N_act;
    }


    double Calculation_HBM3::E_RD(double VDD, double IDD4R, double IDD3N, double t_CK,
                                   std::size_t BL, std::size_t DR, uint64_t N_RD) {
        return VDD * (IDD4R - IDD3N) * (double(BL) / DR) * t_CK * N_RD;
    }

    double Calculation_HBM3::E_WR(double VDD, double IDD4W, double IDD3N, double t_CK,
                                   std::size_t BL, std::size_t DR, uint64_t N_WR) {
        return VDD * (IDD4W - IDD3N) * (double(BL) / DR) * t_CK * N_WR;
    }

    // All-bank refresh energy above the active-standby background the banks already
    // accrue while all of them are held active for tRFC (IDD3N16, see calcEnergy).
    double Calculation_HBM3::E_ref_ab(std::size_t B, double VDD, double IDD5B, double IDD3N16, double tRFC, uint64_t N_REF) {
        return (1.0 / B) * VDD * (IDD5B - IDD3N16) * tRFC * N_REF;
    }

    energy_t Calculation_HBM3::calcEnergy(const MemSpec &memSpec, const SimulationStats &stats) {

        double t_CK = memSpec.memTimingSpec.tCK;
        auto t_RAS = memSpec.memTimingSpec.tRAS * t_CK;
        auto t_RP = memSpec.memTimingSpec.tRP * t_CK;
        auto t_RFC = memSpec.memTimingSpec.tRFC * t_CK;

        auto rho = memSpec.bwParams.bwPowerFactRho;
        auto BL = memSpec.burstLength;
        auto DR = memSpec.dataRate;
        auto B = memSpec.banksPerPseudoChannel;

        auto VDD = memSpec.memPowerSpec.vDD;            // V (core rail, VDDC)
        auto VDDQ = memSpec.memPowerSpec.vDDQ;          // V (I/O rail; == VDD if single-rail)
        // IDD values are specified in mA; convert to A so that
        // V * A * ps = pJ (picojoules)
        auto IDD0    = memSpec.memPowerSpec.iDD0    * 1e-3;
        auto IDD2N   = memSpec.memPowerSpec.iDD2N   * 1e-3;
        auto IDD3N1  = memSpec.memPowerSpec.iDD3N1  * 1e-3;
        auto IDD3N16 = memSpec.memPowerSpec.iDD3N16 * 1e-3;
        auto IDD5B   = memSpec.memPowerSpec.iDD5B   * 1e-3;   // 0 => refresh energy off
        // Read/write currents: optionally replaced by the data-pattern model, which
        // derives an effective IDD4R/IDD4W (mA) from the per-bus activity knobs. When
        // the model is disabled, these are the unchanged config IDD4R/IDD4W.
        double iDD4R_mA = memSpec.memPowerSpec.iDD4R;
        double iDD4W_mA = memSpec.memPowerSpec.iDD4W;
        const auto &dp = memSpec.dataPattern;
        if (dp.enabled) {
            const double idd3n1_mA = memSpec.memPowerSpec.iDD3N1;
            iDD4R_mA = dp.effective_current_mA(iDD4R_mA, idd3n1_mA);
            if (dp.apply_to_writes)
                iDD4W_mA = dp.effective_current_mA(iDD4W_mA, idd3n1_mA);
        }
        auto IDD4R   = iDD4R_mA * 1e-3;
        auto IDD4W   = iDD4W_mA * 1e-3;

        // Split-rail read I/O: only the DQ-pin component of the dynamic read current
        // (IDD4R - IDD3N1) is driven by the I/O rail (VDDQ); the on-die floor/TSV/BG
        // components stay on the core rail (VDD). The DQ share is known only when the
        // data-pattern model is active (it decomposes the current); otherwise the whole
        // read current stays on VDD. When VDDQ == VDD this is exactly the old behaviour.
        const double f_dq_rd = dp.enabled ? dp.dq_energy_fraction() : 0.0;
        const double VDD_RD  = VDDQ * f_dq_rd + VDD * (1.0 - f_dq_rd);

        // For HBM3 IBeta = IDD2N (no explicit precharge operating current)
        auto IBeta = IDD2N;

        // Derived currents
        // I_rho: bank-wise weighted active standby current
        auto I_rho = rho * (IDD3N1 - IDD2N) + IDD2N;

        // I_theta: activation current derived from IDD0 cycle measurement
        auto I_theta = (IDD0 * (t_RP + t_RAS) - IBeta * t_RP) / t_RAS;

        // I_1: single-bank active standby current distributed across all banks
        auto I_1 = (1.0 / B) * (IDD3N1 + (B - 1) * I_rho);

        assert(I_rho >= 0 && "I_rho must be non-negative");
        assert(I_theta >= 0 && "I_theta must be non-negative");
        assert(I_1 >= 0 && "I_1 must be non-negative");

        energy_t energy(memSpec.banksPerPseudoChannel);

        // Active background, same model as HBM2: going from one active bank (IDD3N1)
        // to all B active (IDD3N16) raises active-standby current by
        // (IDD3N16 - IDD3N1) spread over the (B-1) extra banks.
        const double delta_bg_act = (B > 1) ? (IDD3N16 - IDD3N1) / (B - 1) : 0.0;

        // Single pseudo-channel: rank index 0
        const auto &bgFactors  = memSpec.memPowerSpec.bankgroup_scaling_factors;
        const auto &bankFactors = memSpec.memPowerSpec.bank_scaling_factors;
        // Write factors fall back to the read factors when not given.
        const auto &bgWriteFactors = memSpec.memPowerSpec.bankgroup_write_scaling_factors.empty()
            ? bgFactors : memSpec.memPowerSpec.bankgroup_write_scaling_factors;
        const auto &bankWriteFactors = memSpec.memPowerSpec.bank_write_scaling_factors.empty()
            ? bankFactors : memSpec.memPowerSpec.bank_write_scaling_factors;
        const std::size_t banksPerGroup = memSpec.numberOfBanks; // banks per bank-group
        for (std::size_t b = 0; b < B; ++b) {
            const auto &bank = stats.bank[b];

            // Systematic structural variation: scale this bank's read/write current by
            // bankgroup_factor[b / banksPerGroup] * bank_factor[b % banksPerGroup].
            // Empty factor vectors => uniform (factor 1.0), preserving prior behaviour.
            double varFactor = 1.0;
            if (!bgFactors.empty())
                varFactor *= bgFactors[(b / banksPerGroup) % bgFactors.size()];
            if (!bankFactors.empty())
                varFactor *= bankFactors[(b % banksPerGroup) % bankFactors.size()];
            double writeFactor = 1.0;
            if (!bgWriteFactors.empty())
                writeFactor *= bgWriteFactors[(b / banksPerGroup) % bgWriteFactors.size()];
            if (!bankWriteFactors.empty())
                writeFactor *= bankWriteFactors[(b % banksPerGroup) % bankWriteFactors.size()];
            const double IDD4R_b = IDD4R * varFactor;
            const double IDD4W_b = IDD4W * writeFactor;

            energy.bank_energy[b].E_act +=
                E_act(VDD, I_theta, I_1, t_RAS, bank.counter.act);
            energy.bank_energy[b].E_pre +=
                E_pre(VDD, IBeta, IDD2N, t_RP, bank.counter.pre);
            // Each active bank draws delta_bg_act over its active time; with the shared
            // term below, one active bank totals IDD3N1 and all B active total IDD3N16.
            energy.bank_energy[b].E_bg_act +=
                VDD * delta_bg_act * (bank.cycles.activeTime() * t_CK);
            energy.bank_energy[b].E_bg_pre +=
                E_BG_pre(B, VDD, IDD2N, stats.rank_total[0].cycles.pre * t_CK);
            energy.bank_energy[b].E_RD +=
                E_RD(VDD_RD, IDD4R_b, IDD3N1, t_CK, BL, DR, bank.counter.reads);

            energy.bank_energy[b].E_WR +=
                E_WR(VDD, IDD4W_b, IDD3N1, t_CK, BL, DR, bank.counter.writes);

            energy.bank_energy[b].E_RDA +=
                E_RD(VDD_RD, IDD4R_b, IDD3N1, t_CK, BL, DR, bank.counter.readAuto);

            energy.bank_energy[b].E_WRA +=
                E_WR(VDD, IDD4W_b, IDD3N1, t_CK, BL, DR, bank.counter.writeAuto);

            energy.bank_energy[b].E_pre_RDA +=
                E_pre(VDD, IBeta, IDD2N, t_RP, bank.counter.readAuto);

            energy.bank_energy[b].E_pre_WRA +=
                E_pre(VDD, IBeta, IDD2N, t_RP, bank.counter.writeAuto);

            if (IDD5B > 0.0 && t_RFC > 0.0)
                energy.bank_energy[b].E_ref_AB +=
                    E_ref_ab(B, VDD, IDD5B, IDD3N16, t_RFC, bank.counter.refAllBank);
        }

        // Shared base active-standby energy while at least one bank is active; the
        // remaining delta_bg_act of the first active bank comes from its per-bank term.
        energy.E_bg_act_shared +=
            VDD * (IDD3N1 - delta_bg_act) * (stats.rank_total[0].cycles.act * t_CK);

        return energy;
    }

}  // namespace Ramulator::ayna
