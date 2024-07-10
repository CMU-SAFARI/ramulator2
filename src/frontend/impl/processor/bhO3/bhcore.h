#ifndef     RAMULATOR_FRONTEND_PROCESSOR_BH_CORE_H
#define     RAMULATOR_FRONTEND_PROCESSOR_BH_CORE_H

#include <vector>
#include <string>
#include <functional>
#include <filesystem>
#include <fstream>

#include "base/type.h"
#include "base/request.h"
#include "translation/translation.h"

namespace Ramulator {

class BHO3LLC;

class BHO3Core: public Clocked<BHO3Core> {
  friend class BHO3;
  struct Inst {
    int bubble_count = 0;
    Addr_t load_addr = -1;
    Addr_t store_addr = -1;
  };
  
  class Trace {
    friend class BHO3Core;

    std::vector<Inst> m_trace;
    size_t m_trace_length = 0;
    size_t m_curr_trace_idx = 0;

    public:
      Trace(std::string file_path_str);
      const Inst& get_next_inst();
  };

  /**
   * @brief   Simplified ROB of an O3 processor.
   * @details
   * We model
   */
  class InstWindow {
    friend class BHO3Core;
    private:
      int m_ipc = 4;          // How many instructions we can retire in a cycle
      int m_depth = 128;      // How many inflight instructions we can keep track of

      int m_load = 0;         // The current load
      int m_head_idx = 0;     // Head index. New instructions are inserted at the head index.
      int m_tail_idx = 0;     // Tail index. The instruction at the tail will be retired first.

      std::vector<bool>   m_ready_list;   // Bitvector to mark whether each instruction is ready to be retired.
      std::vector<Addr_t> m_addr_list;    // Which address is each LD/ST instruction targeting? TODO: Perf. optimization with unordered map?
      std::vector<Clk_t>  m_depart_list;  // When did this request leave the core?  

    public:
      InstWindow(int ipc = 4, int depth = 128);      

      bool   is_full();

      /**
       * @brief   Inserts an instruction to the window.
       * 
       * @param ready True if instruction is a non-memory instruction. False otherwise.
       * @param addr  -1 if non-memory, the actual LD/ST address otherwise.
       */
      void   insert(bool ready, Addr_t addr, Clk_t clk);

      /**
       * @brief   Tries to retire instructions from the tail of the window
       * 
       * @return int The number of instructions retired.
       */
      int    retire();

      /**
       * @brief   Set a memory instruction to ready. Called by the callback when a request is served by the memory
       * 
       * @return Clk_t depart cycle of this address
       */
      Clk_t  set_ready(Addr_t addr);
  };

  private:
    int m_id = -1;

    Trace m_trace;
    InstWindow m_window;
    ITranslation* m_translation;
    BHO3LLC* m_llc;

    std::function<void(Request&)> m_callback;

    int    m_num_bubbles = 0;
    Addr_t m_load_addr = -1;
    Addr_t m_writeback_addr = -1;

    size_t m_num_expected_insts = 0;  
    uint64_t m_num_max_cycles = 0;
    Clk_t m_last_mem_cycle = 0; // The last cycle that a memory request departs from mc
    int m_lat_hist_sens = 0;
    std::unordered_map<int, uint64_t> m_lat_histogram;
    std::filesystem::path m_dump_path;

    bool m_is_attacker = false;

    void dump_latency_histogram();

  /************************************************
   *              Core Statistics
   ***********************************************/
  public:
    bool reached_expected_num_insts = false;
    size_t s_insts_retired = 0; 
    size_t s_cycles_recorded = 0; 
    size_t s_insts_recorded = 0;
    Clk_t  s_mem_access_cycles = 0; 
    size_t s_mem_requests_issued = 0;

  public:
    BHO3Core(int id, int ipc, int depth,
      size_t num_expected_insts, uint64_t num_max_cycles, std::string trace_path,
      ITranslation* translation, BHO3LLC* llc, int lat_hist_sens, std::string& dump_path, bool is_attacker);

    /**
     * @brief   Ticks the core.
     * 
     */
    void tick() override;

    /**
     * @brief   Called when a request is served by the memory.
     * 
     */
    void receive(Request& req);
};

}        // namespace Ramulator


#endif   // RAMULATOR_FRONTEND_PROCESSOR_BH_CORE_H
