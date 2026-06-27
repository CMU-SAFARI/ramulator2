#ifndef RAMULATOR_FRONTEND_PROCESSOR_BHO3_CORE_H
#define RAMULATOR_FRONTEND_PROCESSOR_BHO3_CORE_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ramulator/base/request.h"
#include "ramulator/base/type.h"
#include "ramulator/translation/i_translation.h"

namespace Ramulator {

class BHO3LLC;

// BHO3Core — same simplified out-of-order pipeline as SimpleO3Core, but
// holds a pointer to BHO3LLC instead of SimpleO3LLC so the LLC's
// blacklist API is callable. Pipeline behavior (trace replay, instruction
// window, retire logic) is identical.
class BHO3Core {
  friend class BHO3;
  class Trace {
    friend class BHO3Core;
    struct Inst {
      int bubble_count = 0;
      Addr_t load_addr = -1;
      Addr_t store_addr = -1;
    };

    std::vector<Inst> m_trace;
    size_t m_trace_length = 0;
    size_t m_curr_trace_idx = 0;

   public:
    Trace(std::string file_path_str);
    const Inst& get_next_inst();
  };

  class InstWindow {
    friend class BHO3Core;

   private:
    int m_ipc = 4;
    int m_depth = 128;

    int m_load = 0;
    int m_head_idx = 0;
    int m_tail_idx = 0;

    std::vector<bool> m_ready_list;
    std::vector<Addr_t> m_addr_list;

   public:
    InstWindow(int ipc = 4, int depth = 128);

    bool is_full();
    void insert(bool ready, Addr_t addr);
    int retire();
    void set_ready(Addr_t addr);
  };

 private:
  const Clk_t& m_clk;
  int m_id = -1;

  Trace m_trace;
  InstWindow m_window;
  ITranslation* m_translation;
  BHO3LLC* m_llc;

  std::function<void(Request&)> m_callback;

  int m_num_bubbles = 0;
  Addr_t m_load_addr = -1;
  Addr_t m_writeback_addr = -1;

  size_t m_num_expected_insts = 0;
  uint64_t m_num_max_cycles = 0;       // hard cap on sim cycles for this core
  Clk_t m_last_mem_cycle = 0;

  // Attacker-tracking metadata
  bool m_is_attacker = false;          // true → core is the synthetic RH attacker
  int m_lat_hist_sens = 0;             // cycles per histogram bucket (0 disables)
  std::string m_dump_path;             // directory for end-of-sim histogram dump (unused for now)
  std::unordered_map<int, uint64_t> m_lat_histogram;  // bucket → count

 public:
  bool reached_expected_num_insts = false;
  bool reached_max_cycles = false;
  size_t s_insts_retired = 0;
  size_t s_insts_recorded = 0;         // instructions retired before reaching expected count
  size_t s_mem_requests_issued = 0;    // memory requests successfully sent to LLC
  size_t s_cycles_recorded = 0;
  Clk_t s_mem_access_cycles = 0;

 public:
  BHO3Core(const Clk_t& clk, int id, int ipc, int depth, size_t num_expected_insts,
           uint64_t num_max_cycles, std::string trace_path, ITranslation* translation,
           BHO3LLC* llc, int lat_hist_sens, std::string dump_path, bool is_attacker);

  void tick();
  void receive(Request& req);

  // True if the core has reached either its instruction count or its
  // cycle cap (the frontend stops when all cores are finished).
  bool is_finished() const {
    return reached_expected_num_insts || reached_max_cycles;
  }

  // Write the per-core latency histogram to "{m_dump_path}/core_{id}_lat_hist.csv".
  // No-op if m_dump_path is empty or m_lat_hist_sens is 0 (histogram disabled).
  // Called by the frontend's finalize() at end of simulation.
  void dump_histogram() const;
};

}  // namespace Ramulator

#endif  // RAMULATOR_FRONTEND_PROCESSOR_BHO3_CORE_H
