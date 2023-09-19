#ifndef     RAMULATOR_FRONTEND_PROCESSOR_CORE_H
#define     RAMULATOR_FRONTEND_PROCESSOR_CORE_H

#include <vector>
#include <string>
#include <functional>

#include "base/type.h"
#include "base/request.h"
#include "translation/translation.h"

namespace Ramulator {

class SimpleO3LLC;

class SimpleO3Core : public Clocked<SimpleO3Core> {
  friend class SimpleO3;
  class Trace {
    friend class SimpleO3Core;
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

  /**
   * @brief   Simplified ROB of an O3 processor.
   * @details
   * We model
   */
  class InstWindow {
    friend class SimpleO3Core;
    private:
      int m_ipc = 4;          // How many instructions we can retire in a cycle
      int m_depth = 128;      // How many inflight instructions we can keep track of

      int m_load = 0;         // The current load
      int m_head_idx = 0;     // Head index. New instructions are inserted at the head index.
      int m_tail_idx = 0;     // Tail index. The instruction at the tail will be retired first.

      std::vector<bool>   m_ready_list;   // Bitvector to mark whether each instruction is ready to be retired.
      std::vector<Addr_t> m_addr_list;    // Which address is each LD/ST instruction targeting? TODO: Perf. optimization with unordered map?

    public:
      InstWindow(int ipc = 4, int depth = 128);      


      bool   is_full();

      /**
       * @brief   Inserts an instruction to the window.
       * 
       * @param ready True if instruction is a non-memory instruction. False otherwise.
       * @param addr  -1 if non-memory, the actual LD/ST address otherwise.
       */
      void   insert(bool ready, Addr_t addr);

      /**
       * @brief   Tries to retire instructions from the tail of the window
       * 
       * @return int The number of instructions retired.
       */
      int    retire();

      /**
       * @brief   Set a memory instruction to ready. Called by the callback when a request is served by the memory
       * 
       */
      void   set_ready(Addr_t addr);
  };

  private:
    int m_id = -1;

    Trace m_trace;
    InstWindow m_window;
    ITranslation* m_translation;
    SimpleO3LLC* m_llc;

    std::function<void(Request&)> m_callback;

    int    m_num_bubbles = 0;
    Addr_t m_load_addr = -1;
    Addr_t m_writeback_addr = -1;

    size_t m_num_expected_insts = 0;  
    Clk_t m_last_mem_cycle = 0; // The last cycle that a memory request departs from mc

  /************************************************
   *              Core Statistics
   ***********************************************/
  public:
    bool reached_expected_num_insts = false;
    size_t s_insts_retired = 0; 
    size_t s_cycles_recorded = 0; 
    Clk_t  s_mem_access_cycles = 0; 

  public:
    SimpleO3Core(int id, int ipc, int depth, size_t num_expected_insts, std::string trace_path, ITranslation* translation, SimpleO3LLC* llc);

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


#endif   // RAMULATOR_FRONTEND_PROCESSOR_CORE_H