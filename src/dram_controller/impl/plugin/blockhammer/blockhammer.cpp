#include "base/base.h"
#include "frontend/frontend.h"
#include "translation/translation.h"
#include "addr_mapper/addr_mapper.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"
#include "frontend/impl/processor/bhO3/bhllc.h"
#include "frontend/impl/processor/bhO3/bhO3.h"

#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <array>
#include <vector>
#include <utility>

#include "blockhammer.h"
#include "blockhammer_util.h"
#include "blockhammer_throttler.h"

namespace Ramulator {

DECLARE_DEBUG_FLAG(DBHPLG);
ENABLE_DEBUG_FLAG(DBHPLG);

Logger_t m_logger;

class BlockHammer : public IControllerPlugin, public Implementation, public IBlockHammer {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, BlockHammer, "BlockHammer", "BlockHammer")

  typedef int elem_t;
  typedef uint16_t ctr_t;
  using SubFilter = CountingBloomFilter<elem_t, ctr_t>;
  using BaseFilter = UnifiedBloomFilter<elem_t, SubFilter>;

  public:
    // blockhammer configuration parameters
    int m_bf_num_filters = -1;
    int m_bf_len_epoch = -1;
    int m_bf_ctr_count = -1;
    int m_bf_ctr_thresh = -1;
    bool m_bf_ctr_saturate = -1;
    int m_bf_num_hashes = -1;
    int m_bf_hist_size = -1;
    int m_bf_hist_max_freq = -1;
    int m_bf_num_rh = -1;
    int m_bf_trefw = -1;
    int m_bf_trc = -1;

    int m_bf_len_epoch_clk = -1;
  
  private:
    IDRAM* m_dram = nullptr;
    BHO3LLC* m_llc;
    std::vector<BaseFilter*> m_filters;
    std::vector<HistoryBuffer<elem_t>*> m_histbufs;
    std::unordered_set<int> m_blacklisted_rows;
    std::vector<std::unordered_map<int ,int>*> m_activations; 
    AttackThrottler* m_attack_throttler;

    int m_clk = -1;
    
    int m_num_mshr_per_core = -1;

    // input parameters
    int m_rank_level = -1;
    int m_bank_group_level = -1;
    int m_bank_level = -1;
    int m_row_level = -1;

    int m_num_ranks = -1;
    int m_num_banks_per_rank = -1;
    int m_num_rows_per_bank = -1;

    bool m_is_debug;

    BaseFilter* get_bank_filter(Request& req) {
      int flat_bank_id = req.addr_vec[m_bank_level];
      int accumulated_dimension = 1;
      for (int i = m_bank_level - 1; i >= m_rank_level; i--) {
        accumulated_dimension *= m_dram->m_organization.count[i + 1];
        flat_bank_id += req.addr_vec[i] * accumulated_dimension;
      }
      return m_filters[flat_bank_id];
    }
  
  public:
    void init() override {
      m_bf_num_filters = param<int>("bf_num_filters").default_val(2);
      m_bf_len_epoch = param<int>("bf_len_epoch").default_val(64000000);
      m_bf_ctr_count = param<int>("bf_ctr_count").default_val(1024);
      m_bf_ctr_thresh = param<int>("bf_ctr_thresh").default_val(128);
      m_bf_ctr_saturate = param<bool>("bf_ctr_saturate").default_val(false);
      m_bf_num_hashes = param<int>("bf_num_hashes").default_val(4);
      m_bf_num_rh = param<int>("bf_num_rh").default_val(16384);
      m_bf_trefw = param<int>("bf_trefw").default_val(64000000);
      m_bf_trc = param<int>("bf_trc").default_val(75);
      m_bf_hist_max_freq = param<int>("bf_hist_max_freq").default_val(1);
      m_is_debug = param<bool>("debug").default_val(false);
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_llc = static_cast<BHO3*>(frontend)->get_llc();
      m_ctrl = cast_parent<IDRAMController>();
      m_dram = m_ctrl->m_dram;

      m_rank_level = m_dram->m_levels("rank");
      m_bank_group_level = m_dram->m_levels("bankgroup");
      m_bank_level = m_dram->m_levels("bank");
      m_row_level = m_dram->m_levels("row");

      m_num_ranks = m_dram->get_level_size("rank");
      m_num_banks_per_rank = m_dram->get_level_size("bankgroup") == -1 ? 
                             m_dram->get_level_size("bank") : 
                             m_dram->get_level_size("bankgroup") * m_dram->get_level_size("bank");
      m_num_rows_per_bank = m_dram->get_level_size("row");

      m_logger = Logging::create_logger("DBHPLG");

      m_num_mshr_per_core = m_llc->get_mshrs_per_core();

      m_bf_len_epoch_clk = m_bf_len_epoch / ((float) m_dram->m_timing_vals("tCK_ps") / 1000.0f);

      float tDelay = (m_bf_len_epoch - (m_bf_ctr_thresh * m_bf_trc));
      tDelay /= ((float) (m_bf_len_epoch / m_bf_trefw) * m_bf_num_rh - m_bf_ctr_thresh);

      m_bf_hist_size = tDelay / ((float) m_dram->m_timing_vals("tCK_ps") / 1000.0f);

      if (m_bf_hist_size < 0) {
        std::cout << "[Ramulator::BlockHammerPlugin] Row History Buffer size must be positive." << std::endl;
        exit(0);
      }

      auto* hash_functions = new std::vector<bloom_hash_fn>();
      uint32_t size = m_bf_ctr_count;
      for (int i = 0; i < m_bf_num_hashes; i++)  {
        hash_functions->push_back([i, size](uint32_t key) {
          uint32_t hash1 = key * 2654435761;
          uint32_t hash2 = hash1 + i * ((key * 2246822519 % (size - 1)) + 1);
          return hash2;
        });
      }

      // TODO: These pointers are currently never deleted.
      for (int i = 0; i < m_num_ranks * m_num_banks_per_rank; i++) {
        auto* sub_filters = new std::vector<SubFilter*>();
        for (int j = 0; j < m_bf_num_filters; j++) {
          sub_filters->push_back(new SubFilter(
            m_bf_ctr_count, m_bf_ctr_thresh,
            m_bf_ctr_saturate, *hash_functions
          ));
        }
        m_filters.push_back(new BaseFilter(*sub_filters, m_bf_len_epoch_clk, m_llc));
        m_activations.push_back(new std::unordered_map<int, int>);
      }
      m_filters[m_num_ranks * m_num_banks_per_rank - 1]->insert(0);
      m_filters[m_num_ranks * m_num_banks_per_rank - 1]->reset();

      for (int i = 0; i < m_num_ranks; i++) {
        m_histbufs.push_back(new HistoryBuffer<elem_t>(m_bf_hist_size, m_bf_hist_max_freq));
      }

      m_attack_throttler = new AttackThrottler(m_llc, m_bf_num_rh, m_bf_ctr_thresh, m_bf_len_epoch_clk,
                                                m_bf_trefw, m_bf_num_filters);

      if (m_is_debug) {
        std::cout << "------------------------------------" << std::endl
                  << "BlockHammer: Initialized" << std::endl;
        std::cout << "num_ranks:                  " << m_num_ranks << std::endl;
        std::cout << "num_banks_per_rank:         " << m_num_banks_per_rank << std::endl;
        std::cout << "num_rows_per_bank:          " << m_num_rows_per_bank << std::endl;
        std::cout << "bf_num_filters:             " << m_bf_num_filters << std::endl;
        std::cout << "bf_len_epoch:               " << m_bf_len_epoch << std::endl;
        std::cout << "bf_ctr_count:               " << m_bf_ctr_count << std::endl;
        std::cout << "bf_ctr_thresh:              " << m_bf_ctr_thresh << std::endl;
        std::cout << "bf_ctr_saturate:            " << m_bf_ctr_saturate << std::endl;
        std::cout << "bf_num_hashes:              " << m_bf_num_hashes << std::endl;
        std::cout << "bf_hist_size:               " << m_bf_hist_size << std::endl;
        std::cout << "bf_hist_max_freq:           " << m_bf_hist_max_freq << std::endl;
        std::cout << "bf_hist_max_freq:           " << m_bf_hist_max_freq << std::endl;
        std::cout << "m_bf_num_rh:                " << m_bf_num_rh << std::endl;
        std::cout << "m_bf_trefw:                 " << m_bf_trefw << std::endl;
        std::cout << "bf_hist_buf_size:           " << m_bf_hist_size << std::endl; 
      }
    }

    void update(bool request_found, ReqBuffer::iterator& req_it) override {
      m_clk++;

      for (int i = 0; i < m_num_ranks; i++) {
        m_histbufs[i]->update();
      }
      for (int i = 0; i < m_num_ranks * m_num_banks_per_rank; i++) {
        m_filters[i]->update();
      }
      m_attack_throttler->update();

      // Nothing to do if we don't have a request.
      if (!request_found) {
        return;
      }

      bool is_opening = m_dram->m_command_meta(req_it->command).is_opening;
      bool is_row = m_dram->m_command_scopes(req_it->command) == m_row_level; 

      // Nothing to do if the request isn't activating a row.
      if (!is_opening || !is_row) {
        return;
      }

      // Update bloom filters and history buffer.
      auto rank_idx = req_it->addr_vec[m_rank_level];
      auto row_addr = req_it->addr_vec[m_row_level];
      m_histbufs[rank_idx]->insert(row_addr);
      auto* filter = get_bank_filter(*req_it);
      filter->insert(row_addr);

      // Update AttackThrottler and Bank Activation Counts
      int flat_bank_id = req_it->addr_vec[m_bank_level];
      int accumulated_dimension = 1;
      for (int i = m_bank_level - 1; i >= m_rank_level; i--) {
        accumulated_dimension *= m_dram->m_organization.count[i + 1];
        flat_bank_id += req_it->addr_vec[i] * accumulated_dimension;
      }
      
      if (filter->test(row_addr)) {
        if (req_it->source_id >= 0) {
          m_attack_throttler->insert(req_it->source_id, flat_bank_id);
          float rhli = m_attack_throttler->get_rhli(req_it->source_id, flat_bank_id);
          m_llc->add_blacklist(req_it->source_id);
          m_llc->set_blacklist_max_mshrs(req_it->source_id, m_num_mshr_per_core * (1 - rhli));
        }
      }
    }

    bool is_act_safe(Request& req) {
      bool is_opening = m_dram->m_command_meta(req.command).is_opening;
      bool is_row = m_dram->m_command_scopes(req.command) == m_row_level; 
      if (is_opening && is_row) {
        auto rank_idx = req.addr_vec[m_rank_level];
        auto row_addr = req.addr_vec[m_row_level];
        auto* filter = get_bank_filter(req);
        bool filter_test = filter->test(row_addr);
        bool histbuf_search = m_histbufs[rank_idx]->search(row_addr);
        return !filter_test || !histbuf_search;
      }
      return true;
    }
};      // class BlockHammer

}       // namespace Ramulator
