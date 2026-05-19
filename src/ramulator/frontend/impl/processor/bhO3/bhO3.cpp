#include "ramulator/frontend/impl/processor/bhO3/bhO3.h"

#include <fmt/format.h>

#include "ramulator/base/param.h"
#include "ramulator/base/utils.h"
#include "ramulator/frontend/impl/processor/bhO3/bhcore.h"
#include "ramulator/frontend/impl/processor/bhO3/bhllc.h"

namespace Ramulator {

// BHO3 — BlockHammer-extended OoO frontend. Identical to SimpleO3 except
// the LLC is a BHO3LLC (which exposes a per-core MSHR-blacklist API),
// and cores are BHO3Core instances that route their misses through the
// new LLC. A BlockHammerController can dynamic_cast its frontend to BHO3
// and call get_llc() to access the blacklist API.

void BHO3::init() {
  RAMULATOR_PARSE_PARAM(m_clock_ratio, unsigned int, "clock_ratio").required();
  RAMULATOR_PARSE_PARAM(m_num_expected_insts, int, "num_expected_insts").required();
  RAMULATOR_PARSE_PARAM(m_num_max_cycles, uint64_t, "num_max_cycles").default_val(0);
  RAMULATOR_PARSE_PARAM(m_traces, std::vector<std::string>, "traces").required();
  RAMULATOR_PARSE_PARAM(m_ipc, int, "ipc").default_val(4);
  RAMULATOR_PARSE_PARAM(m_depth, int, "inst_window_depth").default_val(128);
  RAMULATOR_PARSE_PARAM(m_llc_latency, int, "llc_latency").default_val(47);
  RAMULATOR_PARSE_PARAM(m_llc_linesize_bytes, int, "llc_linesize").default_val(64);
  RAMULATOR_PARSE_PARAM(m_llc_associativity, int, "llc_associativity").default_val(8);
  RAMULATOR_PARSE_PARAM(m_llc_capacity_str, std::string, "llc_capacity_per_core").default_val("2MB");
  RAMULATOR_PARSE_PARAM(m_llc_num_mshr_per_core, int, "llc_num_mshr_per_core").default_val(16);
  RAMULATOR_PARSE_PARAM(m_lat_hist_sens, int, "lat_hist_sens").default_val(0);
  RAMULATOR_PARSE_PARAM(m_dump_path, std::string, "dump_path").default_val("");
  // Per-core attacker IDs (matches upstream's is_attacker_per_core list,
  // re-shaped as the list of attacker core IDs for compactness). Pass
  // [] to mark no cores as attackers; pass [0, 3] to mark cores 0 and 3.
  RAMULATOR_PARSE_PARAM(m_attacker_core_ids, std::vector<int>, "attacker_core_ids")
      .default_val(std::vector<int>{});

  m_num_cores = m_traces.size();
  int llc_capacity_per_core = parse_capacity_str(m_llc_capacity_str);

  // Build a bool[num_cores] from the attacker-id list.
  std::vector<bool> is_attacker(m_num_cores, false);
  for (int id : m_attacker_core_ids) {
    if (id >= 0 && id < m_num_cores) is_attacker[id] = true;
  }

  RAMULATOR_CREATE_CHILD(m_translation, ITranslation);

  m_llc = std::make_unique<BHO3LLC>(m_clk, m_llc_latency,
                                    llc_capacity_per_core * m_num_cores,
                                    m_llc_linesize_bytes, m_llc_associativity,
                                    m_llc_num_mshr_per_core * m_num_cores,
                                    m_num_cores);

  for (int id = 0; id < m_num_cores; id++) {
    auto core = std::make_unique<BHO3Core>(m_clk, id, m_ipc, m_depth, m_num_expected_insts,
                                           m_num_max_cycles, m_traces[id], m_translation,
                                           m_llc.get(), m_lat_hist_sens, m_dump_path,
                                           is_attacker[id]);
    core->m_callback = [this](Request& req) { return this->receive(req); };
    m_cores.push_back(std::move(core));
  }

  m_stats.add("num_expected_insts", m_num_expected_insts);
  m_stats.add("llc_eviction", m_llc->s_llc_eviction);
  m_stats.add("llc_read_access", m_llc->s_llc_read_access);
  m_stats.add("llc_write_access", m_llc->s_llc_write_access);
  m_stats.add("llc_read_misses", m_llc->s_llc_read_misses);
  m_stats.add("llc_write_misses", m_llc->s_llc_write_misses);
  m_stats.add("llc_mshr_unavailable", m_llc->s_llc_mshr_unavailable);
  m_stats.add("llc_mshr_blacklisted", m_llc->s_llc_mshr_blacklisted);

  for (size_t core_id = 0; core_id < m_cores.size(); core_id++) {
    m_stats.add(fmt::format("cycles_recorded_core_{}", core_id), m_cores[core_id]->s_cycles_recorded);
    m_stats.add(fmt::format("memory_access_cycles_recorded_core_{}", core_id),
                m_cores[core_id]->s_mem_access_cycles);
    m_stats.add(fmt::format("insts_recorded_core_{}", core_id), m_cores[core_id]->s_insts_recorded);
    m_stats.add(fmt::format("mem_requests_issued_core_{}", core_id),
                m_cores[core_id]->s_mem_requests_issued);
  }
}

void BHO3::tick() {
  m_clk++;

  constexpr Clk_t kHeartbeatInterval = 10'000'000;
  if (m_clk % kHeartbeatInterval == 0) {
    m_logger.info(fmt::format("BHO3 Heartbeat {} cycles.", m_clk));
  }

  m_llc->tick();
  for (auto& core : m_cores) {
    core->tick();
  }
}

void BHO3::receive(Request& req) {
  m_llc->receive(req);

  for (auto r : m_llc->m_receive_requests[req.addr]) {
    r.arrive = req.arrive;
    r.depart = req.depart;
    m_cores[r.source_id]->receive(r);
  }
  m_llc->m_receive_requests[req.addr].clear();
}

bool BHO3::is_finished() {
  // Done when every core has either retired its expected instruction
  // count or hit the configured per-core cycle cap (m_num_max_cycles).
  for (auto& core : m_cores) {
    if (!core->is_finished()) {
      return false;
    }
  }
  return true;
}

void BHO3::finalize() {
  // Dump per-core latency histograms at end of simulation. Each core
  // writes "{dump_path}/core_{id}_lat_hist.csv" if dump_path is set and
  // lat_hist_sens > 0; otherwise it's a no-op.
  for (auto& core : m_cores) {
    core->dump_histogram();
  }
}

void BHO3::connect_memory_system(IMemorySystem* memory_system) {
  m_llc->connect_memory_system(memory_system);
}

int BHO3::get_num_cores() {
  return m_num_cores;
}

}  // namespace Ramulator
