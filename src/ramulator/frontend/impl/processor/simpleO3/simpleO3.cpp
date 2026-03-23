#include <fmt/format.h>
#include <functional>
#include <memory>

#include "ramulator/base/param.h"
#include "ramulator/base/utils.h"
#include "ramulator/frontend/i_frontend.h"
#include "ramulator/frontend/impl/processor/simpleO3/core.h"
#include "ramulator/frontend/impl/processor/simpleO3/llc.h"
#include "ramulator/translation/i_translation.h"

namespace Ramulator {

// Trace format (one file per core, passed via "traces" param):
// One instruction per line, space-separated.
//   <bubble_count> <load_addr> [store_addr]
//
// - bubble_count: number of non-memory instructions before this memory access
// - load_addr:    load address (decimal or 0x hex)
// - store_addr:   optional store/writeback address (decimal or 0x hex)
//
// Example:
//   3 20734016
//   8 20841280 20841280
//
// The trace replays cyclically.
class SimpleO3 final : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, SimpleO3, "SimpleO3")

 private:
  ITranslation* m_translation;

  int m_num_cores = -1;
  std::vector<std::unique_ptr<SimpleO3Core>> m_cores;
  std::unique_ptr<SimpleO3LLC> m_llc;

  int m_num_expected_insts = 0;
  std::vector<std::string> m_traces;
  int m_ipc = 4;
  int m_depth = 128;
  int m_llc_latency = 47;
  int m_llc_linesize_bytes = 64;
  int m_llc_associativity = 8;
  int m_llc_num_mshr_per_core = 16;
  std::string m_llc_capacity_str;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_clock_ratio, unsigned int, "clock_ratio").required();
    RAMULATOR_PARSE_PARAM(m_num_expected_insts, int, "num_expected_insts").required();
    RAMULATOR_PARSE_PARAM(m_traces, std::vector<std::string>, "traces").required();
    RAMULATOR_PARSE_PARAM(m_ipc, int, "ipc").default_val(4);
    RAMULATOR_PARSE_PARAM(m_depth, int, "inst_window_depth").default_val(128);
    RAMULATOR_PARSE_PARAM(m_llc_latency, int, "llc_latency").default_val(47);
    RAMULATOR_PARSE_PARAM(m_llc_linesize_bytes, int, "llc_linesize").default_val(64);
    RAMULATOR_PARSE_PARAM(m_llc_associativity, int, "llc_associativity").default_val(8);
    RAMULATOR_PARSE_PARAM(m_llc_capacity_str, std::string, "llc_capacity_per_core").default_val("2MB");
    RAMULATOR_PARSE_PARAM(m_llc_num_mshr_per_core, int, "llc_num_mshr_per_core").default_val(16);

    m_num_cores = m_traces.size();
    int llc_capacity_per_core = parse_capacity_str(m_llc_capacity_str);

    RAMULATOR_CREATE_CHILD(m_translation, ITranslation);

    m_llc = std::make_unique<SimpleO3LLC>(m_clk, m_llc_latency, llc_capacity_per_core * m_num_cores,
                                          m_llc_linesize_bytes, m_llc_associativity,
                                          m_llc_num_mshr_per_core * m_num_cores);

    for (int id = 0; id < m_num_cores; id++) {
      auto core = std::make_unique<SimpleO3Core>(m_clk, id, m_ipc, m_depth, m_num_expected_insts, m_traces[id],
                                                 m_translation, m_llc.get());
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

    for (int core_id = 0; core_id < m_cores.size(); core_id++) {
      m_stats.add(fmt::format("cycles_recorded_core_{}", core_id), m_cores[core_id]->s_cycles_recorded);
      m_stats.add(fmt::format("memory_access_cycles_recorded_core_{}", core_id), m_cores[core_id]->s_mem_access_cycles);
    }
  }

  void tick() override {
    m_clk++;

    constexpr Clk_t kHeartbeatInterval = 10'000'000;
    if (m_clk % kHeartbeatInterval == 0) {
      m_logger.info(fmt::format("Processor Heartbeat {} cycles.", m_clk));
    }

    m_llc->tick();
    for (auto& core : m_cores) {
      core->tick();
    }
  }

  void receive(Request& req) {
    m_llc->receive(req);

    // TODO: LLC latency for the core to receive the request?
    for (auto r : m_llc->m_receive_requests[req.addr]) {
      r.arrive = req.arrive;
      r.depart = req.depart;
      m_cores[r.source_id]->receive(r);
    }
    m_llc->m_receive_requests[req.addr].clear();
  };

  bool is_finished() override {
    for (auto& core : m_cores) {
      if (!(core->reached_expected_num_insts)) {
        return false;
      }
    }
    return true;
  }

  void connect_memory_system(IMemorySystem* memory_system) override {
    m_llc->connect_memory_system(memory_system);
  };

  int get_num_cores() override {
    return m_num_cores;
  };
};

}  // namespace Ramulator