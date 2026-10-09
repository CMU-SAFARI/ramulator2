// Ported from Ayna (CMU-SAFARI/HBM-Power@318d3d1), a derivative of DRAMPower.
// Copyright (c) 2026, SAFARI Research Group at ETH Zurich
// Copyright (c) 2022, Technische Universität Kaiserslautern, Fraunhofer IESE
// BSD 3-Clause License, see LICENSE in this directory.
//
// The state variables Ayna's HBM model keeps per bank and per pseudo-channel, unchanged.
// Sources: util/cycle_stats.h, data/stats.h, dram/Bank.h, dram/Rank.h.

#ifndef RAMULATOR_POWER_HBM_STATE_H
#define RAMULATOR_POWER_HBM_STATE_H

#include <cstdint>
#include <optional>
#include <vector>

namespace Ramulator::ayna {

using timestamp_t = uint64_t;

// util/cycle_stats.h
template <typename T>
class interval_counter {
 private:
  T count{0};

  std::optional<T> start;
  std::optional<T> end;

 public:
  interval_counter() = default;
  interval_counter(T start) : start(start){};

 public:
  T get_start() const {
    return start.value_or(0);
  };
  T get_end() const {
    return end.value_or(0);
  };

 public:
  bool is_open() const {
    return start && !end;
  };
  bool is_closed() const {
    return start && end;
  };

  T get_count() const {
    return count;
  };

  T get_count_at(T timestamp) const {
    if (is_open() && timestamp > *start) {
      return count + timestamp - *start;
    }

    return get_count();
  }

  void add(T value) {
    this->count += value;
  };

  uint64_t close_interval(uint64_t timestamp) {
    if (!is_open()) {
      return T{0};
    }

    end = timestamp;
    auto diff = timestamp - *start;
    count += diff;

    return diff;
  }

  void reset_interval() {
    this->start.reset();
    this->end.reset();
  }

  void start_interval(T start) {
    this->start = start;
    this->end.reset();
  }

  void start_interval_if_not_running(T start) {
    if (!is_open()) {
      start_interval(start);
    }
  }
};

using interval_t = interval_counter<uint64_t>;

// data/stats.h — the counters and cycle counts the HBM handlers and formulas use.
// Not ported (no HBM handler or formula reads them): the per-bank, same-bank and two-bank
// refresh counters, power-down and self-refresh cycles, pre/postamble and bus statistics.
struct CycleStats {
  struct command_stats_t {
    uint64_t act = 0;
    uint64_t pre = 0;
    uint64_t reads = 0;
    uint64_t writes = 0;
    uint64_t refAllBank = 0;
    uint64_t readAuto = 0;
    uint64_t writeAuto = 0;
  } counter;

  struct {
    uint64_t act = 0;
    uint64_t pre = 0;
    uint64_t ref = 0;
    uint64_t activeTime() const {
      return act;
    };
  } cycles;
};

struct SimulationStats {
  std::vector<CycleStats> bank;
  std::vector<CycleStats> rank_total;
};

// dram/Bank.h
struct Bank {
 public:
  enum class BankState {
    BANK_PRECHARGED = 0,
    BANK_ACTIVE = 1,
  };

 public:
  CycleStats::command_stats_t counter;

  struct {
    interval_t act;
    interval_t ref;
  } cycles;

  BankState bankState = BankState::BANK_PRECHARGED;

 public:
  timestamp_t latestPre = 0;
  timestamp_t refreshEndTime = 0;
};

// dram/Rank.h — the fields an HBM pseudo-channel uses. Its banks are the pseudo-channel node's
// bank descendants in the DRAM node tree, so Rank::banks, isActive() and countActiveBanks()
// are provided by the plugin, which walks the tree.
struct PseudoChannel {
  struct {
    interval_t act;
  } cycles;
  timestamp_t endRefreshTime = 0;
};

}  // namespace Ramulator::ayna

#endif  // RAMULATOR_POWER_HBM_STATE_H
