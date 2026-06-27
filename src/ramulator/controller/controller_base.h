#ifndef RAMULATOR_CONTROLLER_CONTROLLER_BASE_H
#define RAMULATOR_CONTROLLER_CONTROLLER_BASE_H

#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

#include "ramulator/controller/addr_mapper/i_addr_mapper.h"
#include "ramulator/controller/i_controller.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/controller/scheduler/i_scheduler.h"
#include "ramulator/dram/device.h"

namespace Ramulator {

class IRefreshManager;
class IRowPolicy;
class IFrontEnd;
class IMemorySystem;

// Shared infrastructure for all DRAM controller implementations.
// Provides buffers, stats, sub-component management, and low-level scheduling
// helpers. Subclasses own their tick() policy and protocol-specific behavior.
class ControllerBase : public IController, public Implementation {
 public:
  DRAMDevice m_device;
  IAddrMapper* m_addr_mapper = nullptr;

  // Forwarding methods — bind m_clk for sub-components
  bool check_timing(int command, const AddrVec_t& addr_vec);
  int get_preq_command(int command, const AddrVec_t& addr_vec);

  // IController overrides
  void set_channel_id(int channel_id) override;
  int get_tx_bytes() const override;
  int get_num_levels() const override;
  float get_tCK() const override;

  bool send(Request& req) override;
  bool priority_send(Request& req) override;

  void finalize() override;
  void reset_stats() override;

 protected:
  ControllerBase(const ConfigNode& config, Implementation* parent)
      : Implementation(config, "controller", "ControllerBase", parent) {
  }

  // Shared initialization — call from subclass init()
  void init_base();

  // Shared stats registration — call from subclass setup()
  void setup_base(IFrontEnd* frontend, IMemorySystem* memory_system);

  // Sub-components
  IScheduler* m_scheduler = nullptr;
  IRefreshManager* m_refresh = nullptr;
  IRowPolicy* m_rowpolicy = nullptr;
  std::vector<IControllerPlugin*> m_plugins;

  // Request buffers
  std::deque<Request> m_pending;
  ReqBuffer m_active_buffer;
  ReqBuffer m_priority_buffer;
  ReqBuffer m_read_buffer;
  ReqBuffer m_write_buffer;
  // Efficiently tracks addresses of buffered write requests for write-forwarding
  std::unordered_set<Addr_t> m_buffered_write_addrs;

  // Buffer config
  int m_read_buffer_size;
  int m_write_buffer_size;
  int m_priority_buffer_size;
  float m_wr_low_watermark;
  float m_wr_high_watermark;
  bool m_is_write_mode = false;

  // Cached spec lookups
  int m_bank_level = -1;
  int m_tCK_ps = -1;

  // Per flat-bank count of requests in m_active_buffer (typically 0 or 1).
  // Maintained by promote_to_active / retire_request.
  std::vector<int> m_active_per_bank;

  // Stats
  Clk_t m_measured_clk = 0;

  size_t s_row_hits = 0;
  size_t s_row_misses = 0;
  size_t s_row_conflicts = 0;
  size_t s_read_row_hits = 0;
  size_t s_read_row_misses = 0;
  size_t s_read_row_conflicts = 0;
  size_t s_write_row_hits = 0;
  size_t s_write_row_misses = 0;
  size_t s_write_row_conflicts = 0;

  size_t m_num_cores = 0;
  std::vector<size_t> s_read_row_hits_per_core;
  std::vector<size_t> s_read_row_misses_per_core;
  std::vector<size_t> s_read_row_conflicts_per_core;

  size_t s_num_read_reqs = 0;
  size_t s_num_write_reqs = 0;
  size_t s_num_maintenance_reqs = 0;
  size_t s_num_read_reqs_served = 0;
  size_t s_num_write_reqs_served = 0;
  size_t s_num_maintenance_reqs_served = 0;
  size_t s_num_read_reqs_forwarded = 0;
  size_t s_num_write_reqs_coalesced = 0;
  size_t s_queue_len = 0;
  size_t s_read_queue_len = 0;
  size_t s_write_queue_len = 0;
  size_t s_priority_queue_len = 0;
  float s_queue_len_avg = 0;
  float s_read_queue_len_avg = 0;
  float s_write_queue_len_avg = 0;
  float s_priority_queue_len_avg = 0;

  size_t s_read_latency = 0;
  float s_avg_read_latency = 0;

  float s_read_throughput_MBps = 0;
  float s_write_throughput_MBps = 0;
  float s_total_throughput_MBps = 0;

  // Common tick preamble: advance clock, accumulate queue stats,
  // drain completed reads.
  void tick_prologue();

  // Final command done — move to pending (reads) or remove (writes/maintenance).
  void retire_request(ReqBuffer::iterator& req_it, ReqBuffer& buffer);

  // Opening command done — move request from source buffer to active buffer.
  void promote_to_active(ReqBuffer::iterator& req_it, ReqBuffer& buffer);

  // ── Systematic scheduling ──────────────────────────────────────────
  // Controllers use this helper to ask the scheduler for the best request
  // from a specific buffer under an optional eligibility filter.

  struct Candidate {
    bool valid = false;
    ReqBuffer::iterator it;
    ReqBuffer* buffer = nullptr;
  };

  Candidate pick_best_ready_from(ReqBuffer& buffer, RequestFilterRef filter);
  Candidate pick_priority_if(RequestFilterRef filter = {});
  Candidate pick_rw_if(RequestFilterRef filter = {});

  // Scheduling helpers
  bool would_close_active(const Request& req) const;
  void update_request_stats(ReqBuffer::iterator& req);
  void serve_completed_reads();
  void set_write_mode();
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_CONTROLLER_BASE_H
