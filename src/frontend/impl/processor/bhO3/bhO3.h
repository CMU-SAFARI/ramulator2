#include <functional>
#include <limits>

#include "base/utils.h"
#include "frontend/frontend.h"
#include "translation/translation.h"
#include "frontend/impl/processor/bhO3/bhcore.h"
#include "frontend/impl/processor/bhO3/bhllc.h"


namespace Ramulator {

class BHO3 final : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, BHO3, "BHO3", "Simple timing model OoO processor frontend.")

  private:
    ITranslation*  m_translation;

    int m_num_cores = -1;
    int m_num_blocking_cores = -1;
    std::vector<BHO3Core*> m_cores;
    BHO3LLC* m_llc;

    size_t m_num_expected_insts = 0;
    uint64_t m_num_max_cycles = 0;

    std::string serialization_filename;

  public:
    void init() override {
      m_clock_ratio = param<uint>("clock_ratio").required();
      
      // Core params
      std::vector<std::string> empty_trace;
      std::vector<std::string> trace_list = param<std::vector<std::string>>("traces").desc("A list of traces.").required();
      std::vector<std::string> no_wait_trace_list = param<std::vector<std::string>>("no_wait_traces").desc("Traces that do not block program termination.").default_val(empty_trace);
      m_num_cores = trace_list.size() + no_wait_trace_list.size();
      m_num_blocking_cores = trace_list.size();

      int ipc   = param<int>("ipc").desc("IPC of the SimpleO3 core.").default_val(4);
      int depth = param<int>("inst_window_depth").desc("Instruction window size of the SimpleO3 core.").default_val(128);

      int lat_hist_sensitivity = param<int>("lat_hist_sensitivity").default_val(5);
      std::string lat_dump_path = param<std::string>("lat_dump_path").default_val("");

      // LLC params
      int llc_latency           = param<int>("llc_latency").desc("Aggregated latency of the LLC.").default_val(47);
      int llc_linesize_bytes    = param<int>("llc_linesize").desc("LLC cache line size in bytes.").default_val(64);
      int llc_associativity     = param<int>("llc_associativity").desc("LLC set associativity.").default_val(8);
      int llc_capacity_per_core = parse_capacity_str(param<std::string>("llc_capacity_per_core").desc("LLC capacity per core.").default_val("2MB"));
      int llc_num_mshr_per_core = param<int>("llc_num_mshr_per_core").desc("Number of LLC MSHR entries per core.").default_val(16);

      // Simulation parameters
      m_num_expected_insts = param<int>("num_expected_insts").desc("Number of instructions that the frontend should execute.").required();
      m_num_max_cycles = param<uint64_t>("num_max_cycles").desc("Number of cycles the frontend is allowed to execute.").default_val(std::numeric_limits<uint64_t>::max());

      // Create address translation module
      m_translation = create_child_ifce<ITranslation>();

      // Create the LLC
      m_llc = new BHO3LLC(llc_latency, llc_capacity_per_core * m_num_cores, llc_linesize_bytes, llc_associativity, llc_num_mshr_per_core * m_num_cores, m_num_cores);
      // m_llc->deserialize(serialization_filename);
      // m_llc->serialize(serialization_filename);

      // Create the cores
      std::cout << "Trace ID - Name Mapping:" << std::endl;
      for (int id = 0; id < m_num_cores; id++) {
        bool is_non_blocking = id < m_num_blocking_cores;
        auto& active_list = is_non_blocking ? trace_list : no_wait_trace_list;
        auto active_id = is_non_blocking ? id : (id - m_num_blocking_cores);
        auto* cur_translate = is_non_blocking ? m_translation : nullptr;
        std::cout << "name_trace_" << id << ": " << active_list[active_id] << std::endl;
        BHO3Core* core = new BHO3Core(id, ipc, depth,
          m_num_expected_insts, m_num_max_cycles, active_list[active_id],
          cur_translate, m_llc, lat_hist_sensitivity, lat_dump_path);
        core->m_callback = [this](Request& req){return this->receive(req);} ;
        m_cores.push_back(core);
      }

      m_logger = Logging::create_logger("BHO3");

      // Register the stats
      register_stat(m_num_expected_insts).name("num_expected_insts");
      register_stat(m_llc->s_llc_eviction).name("llc_eviction");
      register_stat(m_llc->s_llc_read_access).name("llc_read_access");
      register_stat(m_llc->s_llc_write_access).name("llc_write_access");
      register_stat(m_llc->s_llc_read_misses).name("llc_read_misses");
      register_stat(m_llc->s_llc_write_misses).name("llc_write_misses");
      register_stat(m_llc->s_llc_mshr_unavailable).name("llc_mshr_unavailable");
      register_stat(m_llc->s_llc_mshr_blacklisted).name("llc_mshr_blacklisted");
      
      for (int core_id = 0; core_id < m_cores.size(); core_id++) {
        register_stat(m_cores[core_id]->s_cycles_recorded).name("cycles_recorded_core_{}", core_id);
        register_stat(m_cores[core_id]->s_insts_recorded).name("insts_recorded_core_{}", core_id);
        register_stat(m_cores[core_id]->s_mem_access_cycles).name("memory_access_cycles_recorded_core_{}", core_id);
        register_stat(m_cores[core_id]->s_mem_requests_issued).name("memory_requests_recorded_core_{}", core_id);
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
    }

    bool is_finished() override {
      for (int i = 0; i < m_num_blocking_cores; i++) {
        auto core = m_cores[i];
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

    // BH Changes Begin
    BHO3LLC* get_llc() {
      return m_llc;
    }

    std::vector<BHO3Core*>& get_cores() {
      return m_cores;
    }
    // BH Changes End
};

}        // namespace Ramulator