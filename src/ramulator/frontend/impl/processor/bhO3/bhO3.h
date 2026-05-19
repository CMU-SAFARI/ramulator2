#ifndef RAMULATOR_FRONTEND_PROCESSOR_BHO3_BHO3_H
#define RAMULATOR_FRONTEND_PROCESSOR_BHO3_BHO3_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/frontend/i_frontend.h"
#include "ramulator/frontend/impl/processor/bhO3/bhcore.h"
#include "ramulator/frontend/impl/processor/bhO3/bhllc.h"
#include "ramulator/translation/i_translation.h"

namespace Ramulator {

// BHO3 — BlockHammer-extended OoO frontend (see bhO3.cpp). The class
// declaration lives in this header so the BlockHammer controller can
// dynamic_cast<BHO3*>(frontend) and reach the LLC's blacklist API via
// get_llc().
class BHO3 final : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, BHO3, "BHO3")

 public:
  void init() override;
  void tick() override;
  void receive(Request& req);
  bool is_finished() override;
  void finalize() override;
  void connect_memory_system(IMemorySystem* memory_system) override;
  int get_num_cores() override;
  BHO3LLC* get_llc() {
    return m_llc.get();
  }

 private:
  ITranslation* m_translation;

  int m_num_cores = -1;
  std::vector<std::unique_ptr<BHO3Core>> m_cores;
  std::unique_ptr<BHO3LLC> m_llc;

  int m_num_expected_insts;
  uint64_t m_num_max_cycles;
  std::vector<std::string> m_traces;
  int m_ipc;
  int m_depth;
  int m_llc_latency;
  int m_llc_linesize_bytes;
  int m_llc_associativity;
  int m_llc_num_mshr_per_core;
  std::string m_llc_capacity_str;
  int m_lat_hist_sens;
  std::string m_dump_path;
  std::vector<int> m_attacker_core_ids;  // ids of cores marked as attackers
};

}  // namespace Ramulator

#endif  // RAMULATOR_FRONTEND_PROCESSOR_BHO3_BHO3_H
