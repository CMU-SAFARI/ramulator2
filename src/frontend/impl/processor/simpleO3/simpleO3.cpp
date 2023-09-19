#include <functional>

#include "base/utils.h"
#include "frontend/frontend.h"
#include "translation/translation.h"
#include "frontend/impl/processor/simpleO3/core.h"
#include "frontend/impl/processor/simpleO3/llc.h"


namespace Ramulator {

class SimpleO3 final : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, SimpleO3, "SimpleO3", "Simple timing model OoO processor frontend.")

  private:
    ITranslation*  m_translation;

    int m_num_cores = -1;
    std::vector<SimpleO3Core*> m_cores;
    SimpleO3LLC* m_llc;

    size_t m_num_expected_insts = 0;

    std::string serialization_filename;


  public:
    void init() override {
      m_clock_ratio = param<uint>("clock_ratio").required();
      
      // Core params
      std::vector<std::string> trace_list = param<std::vector<std::string>>("traces").desc("A list of traces.").required();
      m_num_cores = trace_list.size();

      int ipc   = param<int>("ipc").desc("IPC of the SimpleO3 core.").default_val(4);
      int depth = param<int>("inst_window_depth").desc("Instruction window size of the SimpleO3 core.").default_val(128);

      // LLC params
      int llc_latency           = param<int>("llc_latency").desc("Aggregated latency of the LLC.").default_val(47);
      int llc_linesize_bytes    = param<int>("llc_linesize").desc("LLC cache line size in bytes.").default_val(64);
      int llc_associativity     = param<int>("llc_associativity").desc("LLC set associativity.").default_val(8);
      int llc_capacity_per_core = parse_capacity_str(param<std::string>("llc_capacity_per_core").desc("LLC capacity per core.").default_val("2MB"));
      int llc_num_mshr_per_core = param<int>("llc_num_mshr_per_core").desc("Number of LLC MSHR entries per core.").default_val(16);

      // Simulation parameters
      m_num_expected_insts = param<int>("num_expected_insts").desc("Number of instructions that the frontend should execute.").required();

      // Create address translation module
      m_translation = create_child_ifce<ITranslation>();

      // Create the LLC
      m_llc = new SimpleO3LLC(llc_latency, llc_capacity_per_core * m_num_cores, llc_linesize_bytes, llc_associativity, llc_num_mshr_per_core * m_num_cores);
      // m_llc->deserialize(serialization_filename);
      // m_llc->serialize(serialization_filename);

      // Create the cores
      for (int id = 0; id < m_num_cores; id++) {
        SimpleO3Core* core = new SimpleO3Core(id, ipc, depth, m_num_expected_insts, trace_list[id], m_translation, m_llc);
        core->m_callback = [this](Request& req){return this->receive(req);} ;
        m_cores.push_back(core);
      }

      m_logger = Logging::create_logger("SimpleO3");

      // Register the stats
      register_stat(m_num_expected_insts).name("num_expected_insts");
      register_stat(m_llc->s_llc_eviction).name("llc_eviction");
      register_stat(m_llc->s_llc_read_access).name("llc_read_access");
      register_stat(m_llc->s_llc_write_access).name("llc_write_access");
      register_stat(m_llc->s_llc_read_misses).name("llc_read_misses");
      register_stat(m_llc->s_llc_write_misses).name("llc_write_misses");
      register_stat(m_llc->s_llc_mshr_unavailable).name("llc_mshr_unavailable");
      
      for (int core_id = 0; core_id < m_cores.size(); core_id++) {
        // register_stat(m_cores[core_id]->s_insts_retired).name("cycles_retired_core_{}", core_id);
        register_stat(m_cores[core_id]->s_cycles_recorded).name("cycles_recorded_core_{}", core_id);
        register_stat(m_cores[core_id]->s_mem_access_cycles).name("memory_access_cycles_recorded_core_{}", core_id);
      }
    }

    void tick() override {
      m_clk++;

      if(m_clk % 10000000 == 0) {
        m_logger->info("Processor Heartbeat {} cycles.", m_clk);
      }

      m_llc->tick();
      for (auto core : m_cores) {
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
      for (auto core : m_cores) {
        if (!(core->reached_expected_num_insts)){
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

}        // namespace Ramulator