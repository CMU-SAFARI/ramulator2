// BlockHammer as a controller variant
//   - Per-bank UnifiedBloomFilter
//   - CountingBloomFilter sub-filters using deterministic hash.
//   - Per-rank HistoryBuffer with a fixed-size circular buffer + per-elem
//     counter map
//   - AttackThrottler with rotating-counter design and  RHLI
//     formula (counter / (n_rh * t_cbf / t_refw - n_bl)). Feeds the BHO3
//     LLC blacklist
//
// is_act_safe(req) returns false (block) if the row is hot in the active
// bloom sub-filter and recent in the history buffer
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"
#include "ramulator/dram/dram_spec.h"
#include "ramulator/frontend/impl/processor/bhO3/bhO3.h"
#include "ramulator/frontend/impl/processor/bhO3/bhllc.h"

namespace Ramulator {

namespace {

using bloom_hash_fn = std::function<uint32_t(uint32_t)>;

class CountingBloomFilter {
 public:
  CountingBloomFilter(int num_counters, int ctr_thresh, bool saturate,
                      const std::vector<bloom_hash_fn>& hash_functions)
      : m_hash_functions(hash_functions),
        m_counters(num_counters, 0) {
    this->m_num_counters = num_counters;
    this->m_ctr_thresh = ctr_thresh;
    this->m_saturate = m_saturate;
  }

  void insert(int elem) {
    for (size_t i = 0; i < m_hash_functions.size(); i++) {
      uint32_t idx = m_hash_functions[i]((uint32_t)elem) % m_num_counters;
      if (!m_saturate || m_counters[idx] < (uint32_t)m_ctr_thresh) {
        m_counters[idx]++;
      }
    }
  }

  bool test(int elem) const {
    bool pass = true;
    for (size_t i = 0; i < m_hash_functions.size(); i++) {
      uint32_t idx = m_hash_functions[i]((uint32_t)elem) % m_num_counters;
      pass &= m_counters[idx] >= (uint32_t)m_ctr_thresh;
    }
    return pass;
  }

  void reset() { std::fill(m_counters.begin(), m_counters.end(), 0u); }

 private:
  int m_num_counters;
  int m_ctr_thresh;
  bool m_saturate;
  const std::vector<bloom_hash_fn>& m_hash_functions;
  std::vector<uint32_t> m_counters;
};

class UnifiedBloomFilter {
 public:
  UnifiedBloomFilter(int num_counters, int num_hashes, int ctr_thresh,
                     int num_filters, bool saturate,
                     const std::vector<bloom_hash_fn>& hash_functions,
                     int len_epoch_clk)
      : m_len_epoch(len_epoch_clk) {
    m_filters.reserve(num_filters);
    for (int i = 0; i < num_filters; i++) {
      m_filters.emplace_back(num_counters, ctr_thresh, saturate, hash_functions);
    }
  }

  void update() {
    m_tick++;
    if (m_tick >= m_len_epoch) {
      m_tick = 0;
      m_filters[m_test_idx].reset();
      m_test_idx = (m_test_idx + 1) % m_filters.size();
    }
  }

  void insert(int elem) {
    for (auto& f : m_filters) f.insert(elem);
  }

  bool test(int elem) const { return m_filters[m_test_idx].test(elem); }

  void reset() {
    for (auto& f : m_filters) f.reset();
    m_test_idx = 0;
    m_tick = 0;
  }

 private:
  int m_len_epoch;
  std::vector<CountingBloomFilter> m_filters;
  uint64_t m_tick = 0;
  uint32_t m_test_idx = 0;
};

// Frequency-limited circular history buffer
struct HistoryEntry {
  int entry;
  uint64_t timestamp;
};
class HistoryBuffer {
 public:
  HistoryBuffer(uint32_t size, uint32_t max_freq)
      : m_size(size),
        m_max_freq(max_freq),
        m_history(size, HistoryEntry{-1, (uint64_t)-1}) {}

  bool exists(int elem) const {
    return m_elem_counter.find(elem) != m_elem_counter.end();
  }

  bool exceeds(int elem) {
    return m_elem_counter[elem] >= m_max_freq;
  }

  bool search(int elem) { return exists(elem) && exceeds(elem); }

  void insert(int elem) {
    m_history[m_tick % m_size] = {elem, m_tick};
    if (!exists(elem)) m_elem_counter[elem] = 0;
    m_elem_counter[elem]++;
  }

  void update() {
    m_tick++;
    int evicted = m_history[m_tick % m_size].entry;
    auto it = m_elem_counter.find(evicted);
    if (it == m_elem_counter.end()) return;
    if (--(it->second) == 0) m_elem_counter.erase(it);
  }

 private:
  uint64_t m_tick = 0;
  uint32_t m_size;
  uint32_t m_max_freq;
  std::vector<HistoryEntry> m_history;
  std::unordered_map<int, uint32_t> m_elem_counter;
};

// AttackThrottler
// Per-(thread_id, bank_id) ACT counter, with N rotating maps. update()
// rotates by clearing the active map and advancing the active idx every
// t_cbf cycles. insert() updates ALL N maps; get_rhli reads from the
// active map. RHLI formula:
//   rhli = counter / (n_rh * t_cbf / t_refw - n_bl)
class AttackThrottler {
 public:
  AttackThrottler(int n_rh, int n_bl, int t_cbf, int t_refw, int n_ctrs)
      : m_n_rh(n_rh),
        m_n_bl(n_bl),
        m_t_cbf(t_cbf),
        m_t_refw(t_refw),
        m_n_ctrs(n_ctrs),
        m_act_counters(n_ctrs) {}

  void update() {
    m_clk++;
    if (m_clk >= m_t_cbf) {
      m_clk = 0;
      m_act_counters[m_active_idx].clear();
      m_active_idx = (m_active_idx + 1) % m_n_ctrs;
    }
  }

  void insert(int thread_id, int bank_id) {
    int key = hash(thread_id, bank_id);
    for (int i = 0; i < m_n_ctrs; i++) {
      auto& counter_map = m_act_counters[i];
      if (counter_map.find(key) == counter_map.end()) counter_map[key] = 0;
      counter_map[key]++;
    }
  }

  float get_rhli(int thread_id, int bank_id) const {
    if (thread_id < 0) return 0.0f;
    auto& counter_map = m_act_counters[m_active_idx];
    int key = hash(thread_id, bank_id);
    auto it = counter_map.find(key);
    if (it == counter_map.end()) return 0.0f;
    float denom = (float)m_n_rh * (float)m_t_cbf / (float)m_t_refw - (float)m_n_bl;
    if (denom <= 0.0f) return 0.0f;
    return (float)it->second / denom;
  }

  static int hash(int thread_id, int bank_id) {
    return thread_id * 100000 + bank_id;
  }

 private:
  int m_clk = -1;
  int m_n_rh;
  int m_n_bl;
  int m_t_cbf;
  int m_t_refw;
  int m_n_ctrs;
  int m_active_idx = 0;
  std::vector<std::unordered_map<int, int>> m_act_counters;
};

}  // namespace

class BlockHammerController : public ControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, BlockHammerController, ControllerBase, "BlockHammer")

 public:
  void init() override {
    init_base();

    // Bloom-filter detection params
    RAMULATOR_PARSE_PARAM(m_bf_num_filters, int, "bf_num_filters").default_val(2);
    RAMULATOR_PARSE_PARAM(m_bf_len_epoch, int, "bf_len_epoch").default_val(64000000);
    RAMULATOR_PARSE_PARAM(m_bf_ctr_count, int, "bf_ctr_count").default_val(1024);
    RAMULATOR_PARSE_PARAM(m_bf_ctr_thresh, int, "bf_ctr_thresh").default_val(128);
    RAMULATOR_PARSE_PARAM(m_bf_ctr_saturate, bool, "bf_ctr_saturate").default_val(false);
    RAMULATOR_PARSE_PARAM(m_bf_num_hashes, int, "bf_num_hashes").default_val(4);
    RAMULATOR_PARSE_PARAM(m_bf_hist_max_freq, int, "bf_hist_max_freq").default_val(1);
    // RowHammer-aware sizing knobs — drive the AttackThrottler RHLI
    // formula and the derived bf_hist_size (see setup()).
    RAMULATOR_PARSE_PARAM(m_bf_num_rh, int, "bf_num_rh").default_val(16384);
    RAMULATOR_PARSE_PARAM(m_bf_trefw, int, "bf_trefw").default_val(64000000);
    RAMULATOR_PARSE_PARAM(m_bf_trc, int, "bf_trc").default_val(75);
    RAMULATOR_PARSE_PARAM(m_is_debug, bool, "debug").default_val(false);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    setup_base(frontend, memory_system);

    auto* spec = m_device.m_spec;
    m_rank_level = spec->get_level_id("Rank");
    m_row_level = spec->get_level_id("Row");
    m_num_ranks = spec->get_level_size("Rank");

    int num_banks = m_device.m_bank_nodes.size();

    float ns_per_clk = spec->get_timing_value("tCK_ps") / 1000.0f;
    m_bf_len_epoch_clk = (int)(m_bf_len_epoch / ns_per_clk);

    // Derive bf_hist_size from the RH-aware sizing knobs.
    //   tDelay = (bf_len_epoch - bf_ctr_thresh * bf_trc)
    //          / ((bf_len_epoch / bf_trefw) * bf_num_rh - bf_ctr_thresh)
    //   bf_hist_size = tDelay / ns_per_clk
    {
      float numer = (float)(m_bf_len_epoch - m_bf_ctr_thresh * m_bf_trc);
      float denom_acts =
          (float)(m_bf_len_epoch / m_bf_trefw) * (float)m_bf_num_rh
          - (float)m_bf_ctr_thresh;
      if (denom_acts <= 0.0f) {
        throw std::runtime_error(
            "BlockHammerController: bf_hist_size denominator non-positive — "
            "check bf_len_epoch / bf_trefw / bf_num_rh / bf_ctr_thresh");
      }
      float t_delay = numer / denom_acts;
      m_bf_hist_size = (int)(t_delay / ns_per_clk);
      if (m_bf_hist_size <= 0) {
        throw std::runtime_error(
            "BlockHammerController: derived bf_hist_size must be positive");
      }
    }

    // Bloom-filter hash functions
    uint32_t size = (uint32_t)m_bf_ctr_count;
    m_hash_functions.reserve(m_bf_num_hashes);
    for (int i = 0; i < m_bf_num_hashes; i++) {
      m_hash_functions.emplace_back([i, size](uint32_t key) -> uint32_t {
        uint32_t hash1 = key * 2654435761u;
        uint32_t hash2 = hash1 + (uint32_t)i * ((key * 2246822519u % (size - 1u)) + 1u);
        return hash2;
      });
    }

    // BlockHammer requires the BHO3 frontend
    m_bh_llc = static_cast<BHO3*>(frontend)->get_llc();
    m_num_mshr_per_core = m_bh_llc->get_mshrs_per_core();

    // Per-bank UnifiedBloomFilter, per-rank HistoryBuffer.
    m_bank_filters.reserve(num_banks);
    for (int i = 0; i < num_banks; i++) {
      m_bank_filters.emplace_back(m_bf_ctr_count, m_bf_num_hashes, m_bf_ctr_thresh,
                                  m_bf_num_filters, m_bf_ctr_saturate,
                                  m_hash_functions, m_bf_len_epoch_clk);
    }

    if (num_banks > 0) {
      m_bank_filters[num_banks - 1].insert(0);
      m_bank_filters[num_banks - 1].reset();
    }

    for (int i = 0; i < m_num_ranks; i++) {
      m_rank_history.emplace_back((uint32_t)m_bf_hist_size,
                                  (uint32_t)m_bf_hist_max_freq);
    }

    m_attack_throttler = std::make_unique<AttackThrottler>(
        m_bf_num_rh, m_bf_ctr_thresh, m_bf_len_epoch_clk, m_bf_trefw,
        m_bf_num_filters);

    if (m_is_debug) {
      std::cout << "------------------------------------" << std::endl
                << "BlockHammer: Initialized" << std::endl;
      std::cout << "num_ranks:                  " << m_num_ranks << std::endl;
      std::cout << "num_banks:                  " << num_banks << std::endl;
      std::cout << "bf_num_filters:             " << m_bf_num_filters << std::endl;
      std::cout << "bf_len_epoch:               " << m_bf_len_epoch << std::endl;
      std::cout << "bf_len_epoch_clk:           " << m_bf_len_epoch_clk << std::endl;
      std::cout << "bf_ctr_count:               " << m_bf_ctr_count << std::endl;
      std::cout << "bf_ctr_thresh:              " << m_bf_ctr_thresh << std::endl;
      std::cout << "bf_ctr_saturate:            " << m_bf_ctr_saturate << std::endl;
      std::cout << "bf_num_hashes:              " << m_bf_num_hashes << std::endl;
      std::cout << "bf_hist_size (derived):     " << m_bf_hist_size << std::endl;
      std::cout << "bf_hist_max_freq:           " << m_bf_hist_max_freq << std::endl;
      std::cout << "bf_num_rh:                  " << m_bf_num_rh << std::endl;
      std::cout << "bf_trefw:                   " << m_bf_trefw << std::endl;
      std::cout << "bf_trc:                     " << m_bf_trc << std::endl;
    }
  }

  void tick() override;

 private:
  // Params
  int m_bf_num_filters = -1;
  int m_bf_len_epoch = -1;
  int m_bf_ctr_count = -1;
  int m_bf_ctr_thresh = -1;
  bool m_bf_ctr_saturate = false;
  int m_bf_num_hashes = -1;
  int m_bf_num_rh = -1;
  int m_bf_trefw = -1;
  int m_bf_trc = -1;
  int m_bf_hist_max_freq = -1;
  bool m_is_debug = false;

  // Derived sizing.
  int m_bf_len_epoch_clk = -1;
  int m_bf_hist_size = -1;

  // Spec lookups.
  int m_rank_level = -1;
  int m_row_level = -1;
  int m_num_ranks = -1;

  // Detection state.
  std::vector<bloom_hash_fn> m_hash_functions;
  std::vector<UnifiedBloomFilter> m_bank_filters;
  std::vector<HistoryBuffer> m_rank_history;
  std::unique_ptr<AttackThrottler> m_attack_throttler;

  // Optional BHO3 LLC for MSHR-cap throttling.
  BHO3LLC* m_bh_llc = nullptr;
  int m_num_mshr_per_core = 0;

  // Veto an opening command if the row is hot in the active Bloom
  // sub-filter AND recent in the history buffer.
  bool is_act_safe(const Request& req) {
    auto* spec = m_device.m_spec;
    if (!spec->command_meta[req.command].is_opening) return true;

    int bank = m_device.get_flat_bank_id(req.addr_vec);
    int rank = req.addr_vec[m_rank_level];
    int row = req.addr_vec[m_row_level];

    bool filter_test = m_bank_filters[bank].test(row);
    bool histbuf_search = m_rank_history[rank].search(row);
    return !filter_test || !histbuf_search;
  }

  // Update detection state on every ACT actually issued.
  void observe_act(const Request& req) {
    auto* spec = m_device.m_spec;
    if (!spec->command_meta[req.command].is_opening) return;

    int bank = m_device.get_flat_bank_id(req.addr_vec);
    int rank = req.addr_vec[m_rank_level];
    int row = req.addr_vec[m_row_level];

    m_rank_history[rank].insert(row);
    m_bank_filters[bank].insert(row);

    // If the row is hot AFTER this insert, feed the AttackThrottler and
    // update the BHO3 LLC blacklist + MSHR cap.
    if (m_bank_filters[bank].test(row) && req.source_id >= 0) {
      m_attack_throttler->insert(req.source_id, bank);
      float rhli = m_attack_throttler->get_rhli(req.source_id, bank);
      m_bh_llc->add_blacklist(req.source_id);
      m_bh_llc->set_blacklist_max_mshrs(
          req.source_id,
          (int)(m_num_mshr_per_core * (1.0f - rhli)));
    }
  }
};

void BlockHammerController::tick() {
  // Common bookkeeping
  tick_prologue();

  m_refresh->tick();

  // Pre-schedule hooks
  m_rowpolicy->pre_schedule();
  for (auto* p : m_plugins) {
    p->pre_schedule();
  }

  // Tick all detection state — internal rotation handled inside each.
  for (auto& f : m_bank_filters) f.update();
  for (auto& h : m_rank_history) h.update();
  if (m_attack_throttler) m_attack_throttler->update();

  // Filter for ACT-blocking — opening commands flagged hot AND recent are deferred.
  auto bh_filter = [&](const Request& req) -> bool {
    return is_act_safe(req);
  };

  // Candidate selection: active > priority > read/write
  Candidate cand = pick_best_ready_from(m_active_buffer, {});

  if (!cand.valid) {
    cand = pick_priority_if();
  }

  if (!cand.valid && m_priority_buffer.size() == 0) {
    cand = pick_rw_if(bh_filter);
  }

  if (cand.valid) {
    m_rowpolicy->try_upgrade_command(*cand.it);

    if (!cand.it->is_stat_updated) {
      update_request_stats(cand.it);
    }

    m_device.issue_command(cand.it->command, cand.it->addr_vec, m_clk);

    m_rowpolicy->on_issue(*cand.it);
    for (auto* p : m_plugins) {
      p->on_issue(*cand.it);
    }

    // BlockHammer detection: observe ACTs that actually issued.
    observe_act(*cand.it);

    if (cand.it->command == cand.it->final_command) {
      retire_request(cand.it, *cand.buffer);
    } else if (m_device.m_spec->command_meta[cand.it->command].is_opening) {
      promote_to_active(cand.it, *cand.buffer);
    }
  }

  // Post-schedule hooks
  m_rowpolicy->post_schedule();
  for (auto* p : m_plugins) {
    p->post_schedule();
  }
}

}  // namespace Ramulator
