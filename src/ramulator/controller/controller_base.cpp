#include "ramulator/controller/controller_base.h"

#include <algorithm>
#include <stdexcept>
#include <fmt/format.h>

#include "ramulator/base/param.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"
#include "ramulator/controller/scheduler/i_scheduler.h"
#include "ramulator/dram/dram_spec.h"
#include "ramulator/frontend/i_frontend.h"

namespace Ramulator {

// ── Forwarding methods ──────────────────────────────────────────────────

void ControllerBase::set_channel_id(int channel_id) {
  IController::set_channel_id(channel_id);
  m_device.set_channel_id(channel_id);
}

bool ControllerBase::check_timing(int command, const AddrVec_t& addr_vec) {
  return m_device.check_timing(command, addr_vec, m_clk);
}

int ControllerBase::get_preq_command(int command, const AddrVec_t& addr_vec) {
  return m_device.get_preq_command(command, addr_vec, m_clk);
}

int ControllerBase::get_tx_bytes() const {
  return m_device.m_spec->get_tx_bytes();
}

int ControllerBase::get_num_levels() const {
  return m_device.m_spec->level_count;
}

float ControllerBase::get_tCK() const {
  return m_tCK_ps / 1000.0f;  // ps → ns
}

// ── Shared initialization ───────────────────────────────────────────────

void ControllerBase::init_base() {
  RAMULATOR_PARSE_PARAM(m_wr_low_watermark, float, "wr_low_watermark").default_val(0.2f);
  RAMULATOR_PARSE_PARAM(m_wr_high_watermark, float, "wr_high_watermark").default_val(0.8f);
  RAMULATOR_PARSE_PARAM(m_read_buffer_size, int, "read_buffer_size").default_val(32);
  RAMULATOR_PARSE_PARAM(m_write_buffer_size, int, "write_buffer_size").default_val(32);
  // 1568 = 49 banks (4 BG × 4 banks × ~3 ranks) × 32 entries — large enough for all-bank refresh
  RAMULATOR_PARSE_PARAM(m_priority_buffer_size, int, "priority_buffer_size").default_val(1568);

  m_read_buffer.max_size = m_read_buffer_size;
  m_write_buffer.max_size = m_write_buffer_size;
  m_priority_buffer.max_size = m_priority_buffer_size;

  // Create DRAMSpec and initialize the device
  // RAMULATOR_CHILD: dram
  std::string dram_impl = m_config["dram"]["impl"].as<std::string>();
  m_device.init(DRAMSpec::create(dram_impl, m_config));

  // Cache frequently-used lookups
  m_bank_level = m_device.m_spec->get_level_id("Bank");
  m_tCK_ps = m_device.m_spec->get_timing_value("tCK_ps");

  // Active buffer holds requests with in-flight opening commands (ACT).
  // One request per bank at most, so size to total bank count.
  m_active_buffer.max_size = m_device.m_bank_nodes.size();
  m_active_per_bank.assign(m_device.m_bank_nodes.size(), 0);

  // Create sub-components (must be specified in config — no defaults)
  RAMULATOR_CREATE_CHILD(m_scheduler, IScheduler);
  RAMULATOR_CREATE_CHILD(m_refresh, IRefreshManager);
  RAMULATOR_CREATE_CHILD(m_rowpolicy, IRowPolicy);
  RAMULATOR_CREATE_CHILD(m_addr_mapper, IAddrMapper);

  // Optional plugin list — empty if not configured
  RAMULATOR_CREATE_OPTIONAL_CHILD_LIST(m_plugins, IControllerPlugin);
}

// ── Shared stats registration ───────────────────────────────────────────

void ControllerBase::setup_base(IFrontEnd* frontend, IMemorySystem* memory_system) {
  m_num_cores = frontend->get_num_cores();

  s_read_row_hits_per_core.resize(m_num_cores, 0);
  s_read_row_misses_per_core.resize(m_num_cores, 0);
  s_read_row_conflicts_per_core.resize(m_num_cores, 0);

  m_stats.add("cycles", m_measured_clk);
  m_stats.add("row_hits", s_row_hits);
  m_stats.add("row_misses", s_row_misses);
  m_stats.add("row_conflicts", s_row_conflicts);
  m_stats.add("read_row_hits", s_read_row_hits);
  m_stats.add("read_row_misses", s_read_row_misses);
  m_stats.add("read_row_conflicts", s_read_row_conflicts);
  m_stats.add("write_row_hits", s_write_row_hits);
  m_stats.add("write_row_misses", s_write_row_misses);
  m_stats.add("write_row_conflicts", s_write_row_conflicts);

  for (size_t core_id = 0; core_id < m_num_cores; core_id++) {
    m_stats.add(fmt::format("read_row_hits_core_{}", core_id), s_read_row_hits_per_core[core_id]);
    m_stats.add(fmt::format("read_row_misses_core_{}", core_id), s_read_row_misses_per_core[core_id]);
    m_stats.add(fmt::format("read_row_conflicts_core_{}", core_id), s_read_row_conflicts_per_core[core_id]);
  }

  m_stats.add("num_read_reqs", s_num_read_reqs);
  m_stats.add("num_write_reqs", s_num_write_reqs);
  m_stats.add("num_maintenance_reqs", s_num_maintenance_reqs);
  m_stats.add("num_read_reqs_served", s_num_read_reqs_served);
  m_stats.add("num_write_reqs_served", s_num_write_reqs_served);
  m_stats.add("num_maintenance_reqs_served", s_num_maintenance_reqs_served);
  m_stats.add("num_read_reqs_forwarded", s_num_read_reqs_forwarded);
  m_stats.add("num_write_reqs_coalesced", s_num_write_reqs_coalesced);
  m_stats.add("queue_len", s_queue_len);
  m_stats.add("read_queue_len", s_read_queue_len);
  m_stats.add("write_queue_len", s_write_queue_len);
  m_stats.add("priority_queue_len", s_priority_queue_len);
  m_stats.add("queue_len_avg", s_queue_len_avg);
  m_stats.add("read_queue_len_avg", s_read_queue_len_avg);
  m_stats.add("write_queue_len_avg", s_write_queue_len_avg);
  m_stats.add("priority_queue_len_avg", s_priority_queue_len_avg);

  m_stats.add("read_latency", s_read_latency);
  m_stats.add("avg_read_latency", s_avg_read_latency);

  m_stats.add("read_throughput_MBps", s_read_throughput_MBps);
  m_stats.add("write_throughput_MBps", s_write_throughput_MBps);
  m_stats.add("total_throughput_MBps", s_total_throughput_MBps);
}

// ── IController overrides ───────────────────────────────────────────────

bool ControllerBase::send(Request& req) {
  // Address mapping: addr mapper populates addr_vec from intra_channel_addr.
  // PassThroughAddrMapper is a no-op (addr_vec already set by frontend).
  m_addr_mapper->apply(req);
  req.addr_vec[0] = m_channel_id;

  req.final_command = m_device.m_spec->supported_requests[req.type_id];

  // Forward existing write requests to incoming read requests
  if (req.type_id == Request::Type::Read) {
    if (m_buffered_write_addrs.count(req.addr)) {
      // The request will depart at the next cycle
      req.arrive = m_clk;
      req.depart = m_clk + 1;
      m_pending.push_back(req);
      s_num_read_reqs++;
      s_num_read_reqs_forwarded++;
      return true;
    }
  }

  // Enqueue to corresponding buffer based on request type
  bool is_success = false;
  req.arrive = m_clk;
  if (req.type_id == Request::Type::Read) {
    is_success = m_read_buffer.enqueue(req);
  } else if (req.type_id == Request::Type::Write) {
    // Coalesce: if a write to the same address is already buffered, absorb this one
    // immediately instead of occupying another buffer slot.
    if (m_buffered_write_addrs.count(req.addr)) {
      if (req.callback) {
        req.callback(req);
      }
      s_num_write_reqs++;
      s_num_write_reqs_coalesced++;
      return true;
    }
    is_success = m_write_buffer.enqueue(req);
    if (is_success) m_buffered_write_addrs.insert(req.addr);
  } else {
    throw std::runtime_error(fmt::format(
        "ControllerBase only supports Read (0) and Write (1) request types, got type_id {}", req.type_id));
  }
  if (!is_success) {
    req.arrive = -1;
    return false;
  }

  if (req.type_id == Request::Type::Read) {
    s_num_read_reqs++;
  } else if (req.type_id == Request::Type::Write) {
    s_num_write_reqs++;
  }

  return true;
}

bool ControllerBase::priority_send(Request& req) {
  if (req.final_command < 0 || req.final_command >= m_device.m_spec->command_count) {
    throw std::runtime_error(fmt::format(
        "Invalid priority request final_command {}: expected a DRAM command id in [0, {})",
        req.final_command,
        m_device.m_spec->command_count));
  }

  bool is_success = m_priority_buffer.enqueue(req);
  if (is_success && req.type_id == -1) {
    s_num_maintenance_reqs++;
  }
  return is_success;
}

// ── Tick preamble ───────────────────────────────────────────────────────

void ControllerBase::tick_prologue() {
  m_clk++;
  m_measured_clk++;

  s_queue_len += m_read_buffer.size() + m_write_buffer.size() + m_priority_buffer.size();
  s_read_queue_len += m_read_buffer.size();
  s_write_queue_len += m_write_buffer.size();
  s_priority_queue_len += m_priority_buffer.size();

  serve_completed_reads();
}

// ── Request lifecycle ────────────────────────────────────────────────────

void ControllerBase::retire_request(ReqBuffer::iterator& req_it, ReqBuffer& buffer) {
  if (&buffer == &m_active_buffer) {
    m_active_per_bank[m_device.get_flat_bank_id(req_it->addr_vec)]--;
  }
  if (&buffer == &m_write_buffer) {
    m_buffered_write_addrs.erase(req_it->addr);
  }

  if (req_it->type_id == Request::Type::Read) {
    // Read: completion with read latency
    req_it->depart = m_clk + m_device.m_spec->read_latency;
    m_pending.push_back(*req_it);
    s_num_read_reqs_served++;
  } else if (req_it->type_id == Request::Type::Write) {
    // Write: For now we call the callback here.
    // TODO: We could also do it after a write_latency (e.g., nCWL+nBL)
    // similarily as reads
    if (req_it->callback) {
      req_it->callback(*req_it);
    }
    s_num_write_reqs_served++;
  } else if (req_it->type_id == -1) {
    s_num_maintenance_reqs_served++;
  }
  // Maintenance/direct-command requests are removed once their terminal command issues.
  buffer.remove(req_it);
}

void ControllerBase::promote_to_active(ReqBuffer::iterator& req_it, ReqBuffer& buffer) {
  if (m_active_buffer.enqueue(*req_it)) {
    m_active_per_bank[m_device.get_flat_bank_id(req_it->addr_vec)]++;
    if (&buffer == &m_write_buffer) {
      m_buffered_write_addrs.erase(req_it->addr);
    }
    buffer.remove(req_it);
  }
}

// ── Systematic scheduling ────────────────────────────────────────────────

ControllerBase::Candidate ControllerBase::pick_best_ready_from(
    ReqBuffer& buffer,
    RequestFilterRef filter) {
  Candidate c;
  // Scheduler contract: get_best_request() derives req.command before applying
  // filter, so predicates can inspect the current command directly.
  auto it = m_scheduler->get_best_request(buffer, filter);
  if (it == buffer.end()) {
    return c;
  }
  if (!check_timing(it->command, it->addr_vec)) {
    return c;
  }
  c.valid = true;
  c.it = it;
  c.buffer = &buffer;
  return c;
}

ControllerBase::Candidate ControllerBase::pick_priority_if(RequestFilterRef filter) {
  Candidate c;
  if (m_priority_buffer.size() == 0) {
    return c;
  }

  auto it = m_priority_buffer.begin();
  it->command = get_preq_command(it->final_command, it->addr_vec);
  if (!check_timing(it->command, it->addr_vec)) {
    return c;
  }
  if (would_close_active(*it)) {
    return c;
  }
  if (filter && !filter(*it)) {
    return c;
  }

  c.valid = true;
  c.it = it;
  c.buffer = &m_priority_buffer;
  return c;
}

ControllerBase::Candidate ControllerBase::pick_rw_if(RequestFilterRef filter) {
  set_write_mode();
  auto& buffer = m_is_write_mode ? m_write_buffer : m_read_buffer;
  return pick_best_ready_from(buffer, [&](const Request& req) {
    if (would_close_active(req)) {
      return false;
    }
    return !filter || filter(req);
  });
}

bool ControllerBase::would_close_active(const Request& req) const {
  if (!m_device.m_spec->command_meta[req.command].is_closing) {
    return false;
  }
  if (m_active_buffer.size() == 0) {
    return false;
  }

  auto target = m_device.m_spec->bank_targets[req.command];

  // Hot path: single-bank close (PREpb, RDA, WRA) — O(1) lookup.
  if (target == BankTarget::Single) {
    return m_active_per_bank[m_device.get_flat_bank_id(req.addr_vec)] > 0;
  }

  // All / SameBank: scan occupied banks with proper scope checking.
  // These are rare maintenance commands — linear scan skipping zeros is fine.
  for (int i = 0; i < static_cast<int>(m_active_per_bank.size()); i++) {
    if (m_active_per_bank[i] == 0) {
      continue;
    }
    if (target == BankTarget::SameBank &&
        m_device.m_bank_nodes[i]->m_node_id != req.addr_vec[m_bank_level]) {
      continue;
    }
    if (m_device.bank_matches(m_device.m_bank_nodes[i], req.addr_vec)) {
      return true;
    }
  }
  return false;
}

void ControllerBase::update_request_stats(ReqBuffer::iterator& req) {
  req->is_stat_updated = true;

  if (req->type_id == Request::Type::Read) {
    if (m_device.check_rowbuffer_hit(req->final_command, req->addr_vec, m_clk)) {
      s_read_row_hits++;
      s_row_hits++;
      if (req->source_id != -1) {
        s_read_row_hits_per_core[req->source_id]++;
      }
    } else if (m_device.check_node_open(req->final_command, req->addr_vec, m_clk)) {
      s_read_row_conflicts++;
      s_row_conflicts++;
      if (req->source_id != -1) {
        s_read_row_conflicts_per_core[req->source_id]++;
      }
    } else {
      s_read_row_misses++;
      s_row_misses++;
      if (req->source_id != -1) {
        s_read_row_misses_per_core[req->source_id]++;
      }
    }
  } else if (req->type_id == Request::Type::Write) {
    if (m_device.check_rowbuffer_hit(req->final_command, req->addr_vec, m_clk)) {
      s_write_row_hits++;
      s_row_hits++;
    } else if (m_device.check_node_open(req->final_command, req->addr_vec, m_clk)) {
      s_write_row_conflicts++;
      s_row_conflicts++;
    } else {
      s_write_row_misses++;
      s_row_misses++;
    }
  }
}

void ControllerBase::serve_completed_reads() {
  // Drain all pending requests whose depart time has been reached.
  // The deque is sorted by depart time (m_clk is monotonic, send() runs
  // before tick(), and read_latency > 1), so we can stop at the first
  // request that isn't ready yet.
  while (m_pending.size()) {
    auto& req = m_pending.front();
    if (req.depart > m_clk) {
      break;
    }
    s_read_latency += req.depart - req.arrive;
    if (req.callback) {
      req.callback(req);
    }
    m_pending.pop_front();
  }
}

void ControllerBase::set_write_mode() {
  if (!m_is_write_mode) {
    if ((m_write_buffer.size() > m_wr_high_watermark * m_write_buffer.max_size) || m_read_buffer.size() == 0) {
      m_is_write_mode = true;
    }
  } else {
    if ((m_write_buffer.size() < m_wr_low_watermark * m_write_buffer.max_size) && m_read_buffer.size() != 0) {
      m_is_write_mode = false;
    }
  }
}

void ControllerBase::update_stats() {
  s_avg_read_latency = (s_num_read_reqs_served > 0) ? (float)s_read_latency / (float)s_num_read_reqs_served : 0;

  s_queue_len_avg = (m_measured_clk > 0) ? (float)s_queue_len / (float)m_measured_clk : 0;
  s_read_queue_len_avg = (m_measured_clk > 0) ? (float)s_read_queue_len / (float)m_measured_clk : 0;
  s_write_queue_len_avg = (m_measured_clk > 0) ? (float)s_write_queue_len / (float)m_measured_clk : 0;
  s_priority_queue_len_avg = (m_measured_clk > 0) ? (float)s_priority_queue_len / (float)m_measured_clk : 0;

  int tx_bytes = m_device.m_spec->get_tx_bytes();
  float time_ps = static_cast<float>(m_measured_clk) * m_tCK_ps;
  s_read_throughput_MBps = (time_ps > 0) ? s_num_read_reqs_served * tx_bytes * 1e6f / time_ps : 0;
  s_write_throughput_MBps = (time_ps > 0) ? s_num_write_reqs_served * tx_bytes * 1e6f / time_ps : 0;
  s_total_throughput_MBps = s_read_throughput_MBps + s_write_throughput_MBps;
}

void ControllerBase::finalize() {
  update_stats();
}

void ControllerBase::reset_stats() {
  m_measured_clk = 0;

  s_row_hits = 0;
  s_row_misses = 0;
  s_row_conflicts = 0;
  s_read_row_hits = 0;
  s_read_row_misses = 0;
  s_read_row_conflicts = 0;
  s_write_row_hits = 0;
  s_write_row_misses = 0;
  s_write_row_conflicts = 0;

  std::fill(s_read_row_hits_per_core.begin(), s_read_row_hits_per_core.end(), 0);
  std::fill(s_read_row_misses_per_core.begin(), s_read_row_misses_per_core.end(), 0);
  std::fill(s_read_row_conflicts_per_core.begin(), s_read_row_conflicts_per_core.end(), 0);

  s_num_read_reqs = 0;
  s_num_write_reqs = 0;
  s_num_maintenance_reqs = 0;
  s_num_read_reqs_served = 0;
  s_num_write_reqs_served = 0;
  s_num_maintenance_reqs_served = 0;
  s_num_read_reqs_forwarded = 0;
  s_num_write_reqs_coalesced = 0;

  s_queue_len = 0;
  s_read_queue_len = 0;
  s_write_queue_len = 0;
  s_priority_queue_len = 0;
  s_queue_len_avg = 0;
  s_read_queue_len_avg = 0;
  s_write_queue_len_avg = 0;
  s_priority_queue_len_avg = 0;

  s_read_latency = 0;
  s_avg_read_latency = 0;
  s_read_throughput_MBps = 0;
  s_write_throughput_MBps = 0;
  s_total_throughput_MBps = 0;
}

}  // namespace Ramulator
