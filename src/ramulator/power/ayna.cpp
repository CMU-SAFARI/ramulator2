// Ayna HBM power model as a Ramulator controller plugin.
//
// The handlers, window statistics and formulas are ported from Ayna (CMU-SAFARI/HBM-Power@318d3d1),
// a derivative of DRAMPower: Copyright (c) 2026, SAFARI Research Group at ETH Zurich; Copyright (c)
// 2022, Technische Universität Kaiserslautern, Fraunhofer IESE. BSD 3-Clause License, see
// src/ramulator/power/hbm/LICENSE.

#include <algorithm>
#include <fmt/format.h>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/device.h"
#include "ramulator/dram/dram_spec.h"
#include "ramulator/dram/node.h"
#include "ramulator/power/hbm/calculation.h"
#include "ramulator/power/hbm/energy.h"
#include "ramulator/power/hbm/memspec.h"
#include "ramulator/power/hbm/rate_scaling.h"
#include "ramulator/power/hbm/state.h"
#include "ramulator/power/i_power_model.h"

namespace Ramulator {

/// Ayna HBM power model (HBM2, HBM3, HBM4), fed by the commands this controller issues.
///
/// Each command reaches the model at the tick it completes on (issue tick + command cycles - 1),
/// and commands are applied in completion order.
/// The model's per-bank and per-pseudo-channel state lives on the DRAM node tree, in extension
/// slots of the bank nodes and pseudo-channel nodes. Timings, organization, the DRAM standard and
/// the data rate are read from DRAMSpec. The `power` config carries Ayna's device
/// characterizations; the plugin uses the one for its DRAM standard, at the speedbin's data rate.
///
/// Ayna's implicit commands (the precharge after RDA/WRA and the end of an all-bank refresh) run
/// at their computed time from an event queue on each pseudo-channel, with the same ordering as
/// upstream's implicit-command queue.
///
/// Example config (Python):
///   ramulator.controller_plugin.Ayna(power=ramulator.power.ayna_power())
class Ayna : public IControllerPlugin, public IPowerModel, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, Ayna, "Ayna")

  using timestamp_t = ayna::timestamp_t;
  using Bank = ayna::Bank;

  struct PCExt;

  // Model state of one bank, attached to its bank node.
  struct BankExt final : NodeExtension, ayna::Bank {
    PCExt* pc = nullptr;  // the pseudo-channel this bank belongs to
    int index = -1;       // Ayna's flat bank index within the pseudo-channel
  };

  // Implicit commands, run at their computed time (dram_base's implicit-command queue).
  enum class EventKind { AutoPrecharge, RefreshEnd };
  struct Event {
    timestamp_t timestamp;
    uint64_t seq;  // ties run in insertion order, as upstream
    EventKind kind;
    BankExt* bank;
    bool operator>(const Event& other) const {
      return timestamp != other.timestamp ? timestamp > other.timestamp : seq > other.seq;
    }
  };

  // Model state of one pseudo-channel (Ayna's single-rank HBM device), attached to its node.
  struct PCExt final : NodeExtension, ayna::PseudoChannel {
    DRAMNode* node = nullptr;
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> events;
    uint64_t next_seq = 0;

    // Stats-window baseline (reset_stats)
    double base_total = 0.0;
    ayna::energy_info_t base_info;
  };

  enum class Role { None, ACT, PRE, PREA, RD, WR, RDA, WRA, REFA };
  enum class Model { HBM2, HBM3 };

  struct PendingCommand {
    timestamp_t timestamp;  // completion tick
    Role role;
    int command;
    AddrVec_t addr_vec;
  };

 public:
  void init() override {
    m_ctrl = cast_parent<ControllerBase>();
    m_device = &m_ctrl->m_device;

    // RAMULATOR_CHILD: power
    const ConfigNode power = m_config["power"];
    load_characterization(power, *m_device->m_spec);

    // Ayna's data-pattern model
    const ConfigNode dp = power["dataPattern"];
    auto& m = m_memSpec.dataPattern;
    m.enabled = dp["enabled"].as<bool>();
    m.floor_pJbit = dp["floor_pJbit"].as<double>();
    m.c_dq = dp["c_dq"].as<double>();
    m.c_t2bit = dp["c_t2bit"].as<double>();
    m.c_busflip = dp["c_busflip"].as<double>();
    m.use_coupling = dp["use_coupling"].as<bool>();
    m.c_bg_coupling = dp["c_bg_coupling"].as<double>();
    m.c_tsv_coupling = dp["c_tsv_coupling"].as<double>();
    m.bgcpl_full = dp["bgcpl_full"].as<double>();
    m.tsvcpl_full = dp["tsvcpl_full"].as<double>();
    m.dq_rate = dp["dq_rate"].as<double>();
    m.tsv_rate = dp["tsv_rate"].as<double>();
    m.bg_rate = dp["bg_rate"].as<double>();
    m.ref_dq_rate = dp["ref_dq_rate"].as<double>();
    m.ref_tsv_rate = dp["ref_tsv_rate"].as<double>();
    m.ref_bg_rate = dp["ref_bg_rate"].as<double>();
    m.K = dp["K"].as<double>();
    m.apply_to_writes = dp["apply_to_writes"].as<bool>();
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    const DRAMSpec& spec = *m_device->m_spec;

    // Timings and organization, in controller ticks, read once from DRAMSpec.
    auto& t = m_memSpec.memTimingSpec;
    t.tCK = spec.get_timing_value("tCK_ps");  // the controller tick, in whole ps
    t.tRAS = spec.get_timing_value("nRAS");
    t.tRP = spec.get_timing_value("nRP");
    t.tRL = spec.get_timing_value("nCL");
    t.tWL = spec.get_timing_value("nCWL");
    t.tWR = spec.get_timing_value("nWR");
    t.tRFC = spec.get_timing_value("nRFC");

    m_pc_level = spec.get_level_id("PseudoChannel");
    m_bank_level = spec.get_level_id("Bank");
    m_memSpec.numberOfBanks = spec.get_level_size("Bank");
    m_memSpec.banksPerPseudoChannel = 1;
    for (int level = m_pc_level + 1; level <= m_bank_level; level++) {
      m_memSpec.banksPerPseudoChannel *= spec.organization.level_sizes[level];  // SID flattened
    }

    // Transfers per tick from the burst: burst length (beats) over nBL (ticks), so tBurst = nBL.
    // The speedbin's tCK_ps may be rounded (e.g., 833 ps at 2400 MT/s), so rate * tCK is not used.
    m_memSpec.burstLength = spec.internal_prefetch_size;
    const int nBL = spec.get_timing_value("nBL");
    if (nBL <= 0 || m_memSpec.burstLength % nBL != 0) {
      throw std::runtime_error(
          fmt::format("Ayna: burst length {} is not a whole number of nBL = {} ticks", m_memSpec.burstLength, nBL));
    }
    m_memSpec.dataRate = m_memSpec.burstLength / nBL;
    m_memSpec.derive();
    m_tx_bytes = spec.get_tx_bytes();

    attach_state();
    resolve_roles(spec);
    check_against_ramulator_timing(spec);
    register_stats();
  }

  // ── Command feed ─────────────────────────────────────────────────────

  void on_issue(const Request& req) override {
    const Role role = m_roles[req.command];
    if (role == Role::None) {
      count_unmodelled(req.command);
      return;
    }
    const Clk_t completion = m_ctrl->m_clk + m_device->m_spec->command_cycles[req.command] - 1;
    m_pending.push_back({static_cast<timestamp_t>(completion), role, req.command, req.addr_vec});
  }

  void post_schedule() override {
    // Apply, in completion order, every command that has completed by now.
    std::stable_sort(m_pending.begin(), m_pending.end(),
                     [](const PendingCommand& a, const PendingCommand& b) { return a.timestamp < b.timestamp; });
    const timestamp_t now = m_ctrl->m_clk;
    auto it = m_pending.begin();
    for (; it != m_pending.end() && it->timestamp <= now; ++it) {
      apply(*it);
    }
    m_pending.erase(m_pending.begin(), it);
  }

  // ── Stats ────────────────────────────────────────────────────────────

  void update_stats() override {
    const timestamp_t now = m_ctrl->m_clk;
    s_energy = ayna::energy_info_t{};
    s_E_total = 0.0;
    for_each_pc([&](PCExt& rank) {
      ayna::energy_t energy = calcCoreEnergy(rank, now);
      const double total = energy.total() - rank.base_total;
      s_domain_E_total[rank.node->m_node_id] = total;
      s_E_total += total;
      accumulate(s_energy, energy.total_energy(), rank.base_info);
    });
    s_cycles = now - m_window_start;
    const double duration_ps = static_cast<double>(s_cycles) * m_memSpec.memTimingSpec.tCK;
    s_avg_power_mW = (duration_ps > 0.0) ? (s_E_total / duration_ps) * 1e3 : 0.0;
  }

  void reset_stats() override {
    const timestamp_t now = m_ctrl->m_clk;
    m_window_start = now;
    for_each_pc([&](PCExt& rank) {
      ayna::energy_t energy = calcCoreEnergy(rank, now);
      rank.base_total = energy.total();
      rank.base_info = energy.total_energy();
    });
    s_unmodelled_commands = 0;
    update_stats();
  }

  void finalize() override {
    update_stats();
  }

  // ── IPowerModel ──────────────────────────────────────────────────────

  double energy_pJ() override {
    double total = 0.0;
    for_each_pc([&](PCExt& rank) { total += calcCoreEnergy(rank, m_ctrl->m_clk).total() - rank.base_total; });
    return total;
  }

  double domain_energy_pJ(int domain_id) override {
    double total = 0.0;
    bool found = false;
    for_each_pc([&](PCExt& rank) {
      if (rank.node->m_node_id == domain_id) {
        total = calcCoreEnergy(rank, m_ctrl->m_clk).total() - rank.base_total;
        found = true;
      }
    });
    if (!found) {
      throw std::out_of_range(fmt::format("Ayna: no pseudo-channel {}", domain_id));
    }
    return total;
  }

 private:
  ControllerBase* m_ctrl = nullptr;
  DRAMDevice* m_device = nullptr;

  Model m_model = Model::HBM3;
  int m_pc_level = -1;
  int m_bank_level = -1;
  NodeExtensionSlot<BankExt> m_bank_ext;
  NodeExtensionSlot<PCExt> m_pc_ext;

  ayna::MemSpec m_memSpec{};
  int m_tx_bytes = 0;  // bytes per read or write access
  std::vector<Role> m_roles;
  std::vector<PendingCommand> m_pending;
  std::vector<bool> m_warned_unmodelled;
  timestamp_t m_window_start = 0;

  // Stats (energies in pJ, summed over the channel's pseudo-channels)
  ayna::energy_info_t s_energy;
  double s_E_total = 0.0;
  double s_avg_power_mW = 0.0;
  uint64_t s_cycles = 0;
  uint64_t s_unmodelled_commands = 0;
  std::vector<double> s_domain_E_total;

  // ── Setup helpers ────────────────────────────────────────────────────

  static std::vector<double> parse_factors(const ConfigNode& node) {
    std::vector<double> factors;
    if (node) {
      for (const auto& f : node.seq()) {
        factors.push_back(f.as<double>());
      }
    }
    return factors;
  }

  // The electrical characterization for this DRAM: the `power` config's entry for the DRAM
  // standard, at the speedbin's data rate, then the configured overrides.
  void load_characterization(const ConfigNode& power, const DRAMSpec& spec) {
    const ConfigNode standards = power["standards"];
    const ConfigNode standard = standards[spec.standard_name];

    const std::string model = standard["model"].as<std::string>();
    if (model == "HBM2") {
      m_model = Model::HBM2;
    } else if (model == "HBM3") {
      m_model = Model::HBM3;
    } else {
      throw std::runtime_error("Ayna: unknown model '" + model + "' (expected HBM2 or HBM3)");
    }

    const ConfigNode overrides = power["overrides"];
    auto value = [&](const char* name, double base) {
      const ConfigNode o = overrides[name];
      return o ? o.as<double>() : base;
    };

    auto& p = m_memSpec.memPowerSpec;
    p.vDD = value("vDD", standard["vDD"].as<double>());
    p.vDDQ = value("vDDQ", standard["vDDQ"].as<double>(p.vDD));

    const int rate = spec.get_timing_value("rate");
    if (standard["rate_scaling"].as<bool>(false)) {
      if (!ayna::rate_scaling::in_studied_range(rate)) {
        m_logger.warn(fmt::format(
            "Ayna: {} currents extrapolated to {} MT/s, outside the {}-{} MT/s range of "
            "Ayna's data-rate study",
            spec.standard_name, rate, ayna::rate_scaling::STUDIED_MIN_MTS, ayna::rate_scaling::STUDIED_MAX_MTS));
      }
      ayna::rate_scaling::device_currents(rate, p);
    } else {
      const int characterized = standard["rate"].as<int>();
      if (rate != characterized) {
        m_logger.warn(fmt::format("Ayna: {} currents are characterized at {} MT/s; using them at {} MT/s",
                                  spec.standard_name, characterized, rate));
      }
      p.iDD0 = standard["iDD0"].as<double>();
      p.iDD2N = standard["iDD2N"].as<double>();
      p.iDD3N1 = standard["iDD3N1"].as<double>();
      p.iDD3N16 = standard["iDD3N16"].as<double>();
      p.iDD4R = standard["iDD4R"].as<double>();
      p.iDD4W = standard["iDD4W"].as<double>();
      p.iDD5B = standard["iDD5B"].as<double>(0.0);
    }
    p.iDD0 = value("iDD0", p.iDD0);
    p.iDD2N = value("iDD2N", p.iDD2N);
    p.iDD3N1 = value("iDD3N1", p.iDD3N1);
    p.iDD3N16 = value("iDD3N16", p.iDD3N16);
    p.iDD4R = value("iDD4R", p.iDD4R);
    p.iDD4W = value("iDD4W", p.iDD4W);
    p.iDD5B = value("iDD5B", p.iDD5B);

    if (power["variation"].as<bool>(false)) {
      const ConfigNode variation = standard["variation"];
      if (!variation) {
        throw std::runtime_error("Ayna has no measured variation factors for " + spec.standard_name);
      }
      p.bankgroup_scaling_factors = parse_factors(variation["bankgroup_scaling_factors"]);
      p.bank_scaling_factors = parse_factors(variation["bank_scaling_factors"]);
      p.bankgroup_write_scaling_factors = parse_factors(variation["bankgroup_write_scaling_factors"]);
      p.bank_write_scaling_factors = parse_factors(variation["bank_write_scaling_factors"]);
    }
  }

  void attach_state() {
    m_bank_ext = m_device->register_node_extension<BankExt>();
    m_pc_ext = m_device->register_node_extension<PCExt>();

    // Every pseudo-channel gets state, including those that never receive a command: device power
    // is the sum over all pseudo-channels, idle ones included.
    int num_pcs = 0;
    m_device->m_root->for_each_at_level(m_pc_level, [&](DRAMNode* pc_node) {
      PCExt& rank = m_pc_ext.attach(pc_node);
      rank.node = pc_node;
      int index = 0;
      pc_node->for_each_at_level(m_bank_level, [&](DRAMNode* bank_node) {
        BankExt& bank = m_bank_ext.attach(bank_node);
        bank.pc = &rank;
        bank.index = index++;
      });
      num_pcs = std::max(num_pcs, pc_node->m_node_id + 1);
    });
    s_domain_E_total.assign(num_pcs, 0.0);
  }

  void resolve_roles(const DRAMSpec& spec) {
    const std::pair<const char*, Role> roles[] = {
        {"ACT", Role::ACT}, {"PREpb", Role::PRE}, {"PREab", Role::PREA}, {"RD", Role::RD},
        {"WR", Role::WR},   {"RDA", Role::RDA},   {"WRA", Role::WRA},    {"REFab", Role::REFA},
    };
    m_roles.assign(spec.command_count, Role::None);
    for (const auto& [name, role] : roles) {
      if (spec.has_command(name)) {
        m_roles[spec.get_command_id(name)] = role;
      }
    }
    m_warned_unmodelled.assign(spec.command_count, false);
  }

  // The timing-based events reproduce Ayna's model only if no command reaches a bank before its
  // pending event: an ACT before the auto-precharge or before the refresh ends. Ramulator's timing
  // constraints, measured between command completions, must rule that out.
  void check_against_ramulator_timing(const DRAMSpec& spec) {
    auto completion_gap = [&](const char* prev_name, const char* next_name) -> long {
      if (!spec.has_command(prev_name) || !spec.has_command(next_name)) {
        return -1;
      }
      const int prev = spec.get_command_id(prev_name);
      const int next = spec.get_command_id(next_name);
      long gap = 0;
      for (int level = 0; level < spec.level_count; level++) {
        for (const auto& t : spec.timing_cons[level][prev]) {
          if (t.cmd == next && t.window == 1 && !t.sibling) {
            gap = std::max<long>(gap, t.val + (spec.command_cycles[next] - 1) - (spec.command_cycles[prev] - 1));
          }
        }
      }
      return gap;
    };
    auto require = [&](const char* prev, const char* next, uint64_t needed, const char* what) {
      const long gap = completion_gap(prev, next);
      if (gap >= 0 && gap < static_cast<long>(needed)) {
        throw std::runtime_error(
            fmt::format("Ayna: {} -> {} can complete {} ticks apart, but the model needs {} ({}); "
                        "the power model would diverge for these timings",
                        prev, next, gap, needed, what));
      }
    };
    const auto& t = m_memSpec.memTimingSpec;
    require("ACT", "ACT", t.tRAS, "tRAS before the auto-precharge");
    require("RDA", "ACT", m_memSpec.prechargeOffsetRD, "the RDA auto-precharge offset");
    require("WRA", "ACT", m_memSpec.prechargeOffsetWR, "the WRA auto-precharge offset");
    require("REFab", "ACT", t.tRFC, "tRFC");
  }

  void register_stats() {
    m_stats.add("E_act", s_energy.E_act);
    m_stats.add("E_pre", s_energy.E_pre);
    m_stats.add("E_bg_act", s_energy.E_bg_act);
    m_stats.add("E_bg_pre", s_energy.E_bg_pre);
    m_stats.add("E_RD", s_energy.E_RD);
    m_stats.add("E_WR", s_energy.E_WR);
    m_stats.add("E_RDA", s_energy.E_RDA);
    m_stats.add("E_WRA", s_energy.E_WRA);
    m_stats.add("E_pre_RDA", s_energy.E_pre_RDA);
    m_stats.add("E_pre_WRA", s_energy.E_pre_WRA);
    m_stats.add("E_ref_AB", s_energy.E_ref_AB);
    m_stats.add("E_total", s_E_total);
    m_stats.add("avg_power_mW", s_avg_power_mW);
    m_stats.add("cycles", s_cycles);
    m_stats.add("unmodelled_commands", s_unmodelled_commands);
    m_stats.add("domain_E_total", s_domain_E_total);
    m_stats.add("tx_bytes", m_tx_bytes);

    // The electrical characterization in use (V, mA)
    const auto& p = m_memSpec.memPowerSpec;
    m_stats.add("vDD", p.vDD);
    m_stats.add("vDDQ", p.vDDQ);
    m_stats.add("iDD0", p.iDD0);
    m_stats.add("iDD2N", p.iDD2N);
    m_stats.add("iDD3N1", p.iDD3N1);
    m_stats.add("iDD3N16", p.iDD3N16);
    m_stats.add("iDD4R", p.iDD4R);
    m_stats.add("iDD4W", p.iDD4W);
    m_stats.add("iDD5B", p.iDD5B);
  }

  // ── State access ─────────────────────────────────────────────────────

  BankExt& bank_ext(DRAMNode* bank_node) const {
    return m_bank_ext.get(bank_node);
  }

  template <typename Func>
  void for_each_pc(Func&& fn) {
    m_device->m_root->for_each_at_level(m_pc_level, [&](DRAMNode* pc_node) { fn(m_pc_ext.get(pc_node)); });
  }

  // ── Applying commands ────────────────────────────────────────────────

  // dram_base::doCoreCommand: run the implicit commands due by now, then the command.
  void apply(const PendingCommand& c) {
    const timestamp_t timestamp = c.timestamp;
    switch (c.role) {
      case Role::PREA:
      case Role::REFA: {
        // Ramulator resolves the target banks; Ayna's rank handlers visit them pseudo-channel by
        // pseudo-channel in the same order (handlePreAll / handleRefAll).
        std::vector<PCExt*> touched;
        m_device->for_each_target_bank(c.command, c.addr_vec, [&](int bank_id) {
          BankExt& bank = bank_ext(m_device->m_bank_nodes[bank_id]);
          PCExt& rank = *bank.pc;
          if (touched.empty() || touched.back() != &rank) {
            advance(rank, timestamp);
            touched.push_back(&rank);
          }
          if (c.role == Role::PREA) {
            handlePre(rank, bank, timestamp);
          } else {
            handleRefreshOnBank(rank, bank, timestamp, m_memSpec.memTimingSpec.tRFC, bank.counter.refAllBank);
          }
        });
        if (c.role == Role::REFA) {
          for (PCExt* rank : touched) {
            rank->endRefreshTime = timestamp + m_memSpec.memTimingSpec.tRFC;
          }
        }
        return;
      }
      default: {
        BankExt& bank = bank_ext(m_device->m_bank_nodes[m_device->get_flat_bank_id(c.addr_vec)]);
        PCExt& rank = *bank.pc;
        advance(rank, timestamp);
        switch (c.role) {
          case Role::ACT:
            handleAct(rank, bank, timestamp);
            break;
          case Role::PRE:
            handlePre(rank, bank, timestamp);
            break;
          case Role::RD:
            handleRead(rank, bank, timestamp);
            break;
          case Role::WR:
            handleWrite(rank, bank, timestamp);
            break;
          case Role::RDA:
            handleReadAuto(rank, bank, timestamp);
            break;
          case Role::WRA:
            handleWriteAuto(rank, bank, timestamp);
            break;
          default:
            break;
        }
        return;
      }
    }
  }

  void count_unmodelled(int command) {
    s_unmodelled_commands++;
    if (!m_warned_unmodelled[command]) {
      m_warned_unmodelled[command] = true;
      m_logger.warn(fmt::format("Ayna does not model {}; its energy and busy time are not counted",
                                m_device->m_spec->command_names[command]));
    }
  }

  // ── Implicit commands as timing-based events (dram_base's implicit-command queue) ──

  void addImplicitCommand(PCExt& rank, timestamp_t timestamp, EventKind kind, BankExt& bank) {
    rank.events.push({timestamp, rank.next_seq++, kind, &bank});
  }

  // dram_base::processImplicitCommandQueue: run every event with time <= timestamp, in order.
  void advance(PCExt& rank, timestamp_t timestamp) {
    while (!rank.events.empty() && rank.events.top().timestamp <= timestamp) {
      const Event event = rank.events.top();
      rank.events.pop();
      BankExt& bank = *event.bank;
      const timestamp_t t = event.timestamp;
      switch (event.kind) {
        case EventKind::AutoPrecharge:
          handlePre(rank, bank, t);
          break;
        case EventKind::RefreshEnd:
          // The implicit command HBM2/HBM3::handleRefreshOnBank queues.
          bank.bankState = Bank::BankState::BANK_PRECHARGED;
          bank.cycles.act.close_interval(t);
          if (!isActive(rank, t)) {
            rank.cycles.act.close_interval(t);
          }
          break;
      }
    }
  }

  // ── Ported from standards/hbm3/HBM3.cpp (HBM2.cpp is identical) ──

  // Rank::isActive / Rank::countActiveBanks; the rank's banks are the pseudo-channel's bank nodes.
  bool isActive(PCExt& rank, timestamp_t timestamp) {
    return countActiveBanks(rank) > 0 || timestamp < rank.endRefreshTime;
  }

  std::size_t countActiveBanks(PCExt& rank) {
    std::size_t count = 0;
    rank.node->for_each_at_level(m_bank_level, [&](DRAMNode* node) {
      if (bank_ext(node).bankState == Bank::BankState::BANK_ACTIVE) {
        count++;
      }
    });
    return count;
  }

  void handleAct(PCExt& rank, Bank& bank, timestamp_t timestamp) {
    bank.counter.act++;
    bank.cycles.act.start_interval(timestamp);

    if (!isActive(rank, timestamp)) {
      rank.cycles.act.start_interval(timestamp);
    }

    bank.bankState = Bank::BankState::BANK_ACTIVE;
  }

  void handlePre(PCExt& rank, Bank& bank, timestamp_t timestamp) {
    if (bank.bankState == Bank::BankState::BANK_PRECHARGED) {
      return;
    }

    bank.counter.pre++;
    bank.cycles.act.close_interval(timestamp);
    bank.latestPre = timestamp;
    bank.bankState = Bank::BankState::BANK_PRECHARGED;

    if (!isActive(rank, timestamp)) {
      rank.cycles.act.close_interval(timestamp);
    }
  }

  // All-bank refresh: every bank is busy (active-like) for tRFC, then implicitly precharged.
  void handleRefreshOnBank(PCExt& rank, BankExt& bank, timestamp_t timestamp, uint64_t timing, uint64_t& counter) {
    ++counter;
    if (!isActive(rank, timestamp)) {
      rank.cycles.act.start_interval(timestamp);
    }
    bank.bankState = Bank::BankState::BANK_ACTIVE;
    auto timestamp_end = timestamp + timing;
    bank.refreshEndTime = timestamp_end;
    if (!bank.cycles.act.is_open()) {
      bank.cycles.act.start_interval(timestamp);
    }
    addImplicitCommand(rank, timestamp_end, EventKind::RefreshEnd, bank);
  }

  void handleRead(PCExt&, Bank& bank, timestamp_t) {
    ++bank.counter.reads;
  }

  void handleWrite(PCExt&, Bank& bank, timestamp_t) {
    ++bank.counter.writes;
  }

  void handleReadAuto(PCExt& rank, BankExt& bank, timestamp_t timestamp) {
    ++bank.counter.readAuto;

    auto minBankActiveTime = bank.cycles.act.get_start() + m_memSpec.memTimingSpec.tRAS;
    auto minReadActiveTime = timestamp + m_memSpec.prechargeOffsetRD;
    auto delayed_timestamp = std::max(minBankActiveTime, minReadActiveTime);

    addImplicitCommand(rank, delayed_timestamp, EventKind::AutoPrecharge, bank);
  }

  void handleWriteAuto(PCExt& rank, BankExt& bank, timestamp_t timestamp) {
    ++bank.counter.writeAuto;

    auto minBankActiveTime = bank.cycles.act.get_start() + m_memSpec.memTimingSpec.tRAS;
    auto minWriteActiveTime = timestamp + m_memSpec.prechargeOffsetWR;
    auto delayed_timestamp = std::max(minBankActiveTime, minWriteActiveTime);

    addImplicitCommand(rank, delayed_timestamp, EventKind::AutoPrecharge, bank);
  }

  // HBM2/HBM3::getWindowStats
  ayna::SimulationStats getWindowStats(PCExt& rank, timestamp_t timestamp) {
    advance(rank, timestamp);  // processImplicitCommandQueue(timestamp)

    ayna::SimulationStats stats;
    auto B = m_memSpec.banksPerPseudoChannel;
    stats.bank.resize(B);
    stats.rank_total.resize(1);

    auto simulation_duration = timestamp;

    rank.node->for_each_at_level(m_bank_level, [&](DRAMNode* node) {
      const BankExt& bank = bank_ext(node);
      const std::size_t b = bank.index;
      stats.bank[b].counter = bank.counter;
      stats.bank[b].cycles.act = bank.cycles.act.get_count_at(timestamp);
      stats.bank[b].cycles.ref = bank.cycles.ref.get_count_at(timestamp);
      stats.bank[b].cycles.pre = simulation_duration - stats.bank[b].cycles.act;
    });

    stats.rank_total[0].cycles.act = rank.cycles.act.get_count_at(timestamp);
    stats.rank_total[0].cycles.pre = simulation_duration - rank.cycles.act.get_count_at(timestamp);

    return stats;
  }

  // HBM2/HBM3::calcCoreEnergy (HBM4 runs on the HBM3 formulas, as in Ayna)
  ayna::energy_t calcCoreEnergy(PCExt& rank, timestamp_t timestamp) {
    const ayna::SimulationStats stats = getWindowStats(rank, timestamp);
    if (m_model == Model::HBM2) {
      ayna::Calculation_HBM2 calculation;
      return calculation.calcEnergy(m_memSpec, stats);
    }
    ayna::Calculation_HBM3 calculation;
    return calculation.calcEnergy(m_memSpec, stats);
  }

  static void accumulate(ayna::energy_info_t& sum, const ayna::energy_info_t& e, const ayna::energy_info_t& base) {
    sum.E_act += e.E_act - base.E_act;
    sum.E_pre += e.E_pre - base.E_pre;
    sum.E_bg_act += e.E_bg_act - base.E_bg_act;
    sum.E_bg_pre += e.E_bg_pre - base.E_bg_pre;
    sum.E_RD += e.E_RD - base.E_RD;
    sum.E_WR += e.E_WR - base.E_WR;
    sum.E_RDA += e.E_RDA - base.E_RDA;
    sum.E_WRA += e.E_WRA - base.E_WRA;
    sum.E_pre_RDA += e.E_pre_RDA - base.E_pre_RDA;
    sum.E_pre_WRA += e.E_pre_WRA - base.E_pre_WRA;
    sum.E_ref_AB += e.E_ref_AB - base.E_ref_AB;
  }
};

}  // namespace Ramulator
