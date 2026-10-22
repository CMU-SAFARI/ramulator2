#include <algorithm>
#include <fmt/format.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"

namespace Ramulator {

// DDR controller that also serves processing-using-DRAM (PUM) requests.
//
// Every request type the DRAM standard declares beyond Read and Write is a PUM
// request; its terminal command comes from the standard's supported_requests.
// PUM requests carry a full addr_vec naming their target subarray, wait in a
// dedicated buffer, and complete when their command's latency has elapsed. That
// latency is the command's Subarray-level constraint on itself in the
// standard's timing constraints.
class PUMController : public ControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, PUMController, ControllerBase, "PUM")

 public:
  void init() override;
  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override;
  void tick() override;
  bool send(Request& req) override;

 private:
  // A PUM request whose command has issued, until its latency has elapsed.
  struct InFlight {
    Request req;
    Clk_t depart;
  };

  bool is_pum_type(int type_id) const {
    return type_id >= m_first_pum_type_id;
  }

  void retire_pum(ReqBuffer::iterator& req_it);
  void serve_completed_pum();

  ReqBuffer m_pum_buffer;
  std::vector<InFlight> m_pum_in_flight;

  int m_pum_buffer_size = 0;
  int m_first_pum_type_id = 2;       // Read = 0, Write = 1, PUM types follow
  std::vector<Clk_t> m_pum_latency;  // per PUM type

  std::vector<std::string> m_pum_type_names;
  std::vector<size_t> s_pum_reqs;
  std::vector<size_t> s_pum_reqs_served;
  size_t s_pum_total_reqs = 0;
  size_t s_pum_total_reqs_served = 0;
  size_t s_pum_queue_len = 0;
  size_t s_pum_occupancy_cycles = 0;
};

void PUMController::init() {
  init_base();

  RAMULATOR_PARSE_PARAM(m_pum_buffer_size, int, "pum_buffer_size").default_val(32);
  m_pum_buffer.max_size = m_pum_buffer_size;

  const auto& spec = *m_device.m_spec;
  if (!spec.has_level("Subarray")) {
    throw std::runtime_error(fmt::format(
        "The PUM controller requires a DRAM standard with a Subarray level; '{}' has none.", spec.standard_name));
  }
  const int subarray_level = spec.get_level_id("Subarray");

  const int num_types = static_cast<int>(spec.supported_requests.size());
  if (num_types <= m_first_pum_type_id) {
    throw std::runtime_error(
        fmt::format("The PUM controller requires a DRAM standard declaring request types beyond Read and Write; '{}' "
                    "declares only {}.",
                    spec.standard_name, num_types));
  }

  for (int type_id = m_first_pum_type_id; type_id < num_types; type_id++) {
    const int command = spec.supported_requests[type_id];
    const auto& cons = spec.timing_cons[subarray_level][command];
    auto self = std::find_if(cons.begin(), cons.end(), [&](const TimingConsEntry& e) { return e.cmd == command; });
    if (self == cons.end()) {
      throw std::runtime_error(fmt::format(
          "DRAM standard '{}' has no Subarray-level {} -> {} constraint, so the "
          "latency of PUM command {} is undefined.",
          spec.standard_name, spec.command_names[command], spec.command_names[command], spec.command_names[command]));
    }
    m_pum_latency.push_back(self->val);
    m_pum_type_names.push_back(spec.command_names[command]);
  }
  s_pum_reqs.assign(m_pum_type_names.size(), 0);
  s_pum_reqs_served.assign(m_pum_type_names.size(), 0);
}

void PUMController::setup(IFrontEnd* frontend, IMemorySystem* memory_system) {
  setup_base(frontend, memory_system);

  for (size_t i = 0; i < m_pum_type_names.size(); i++) {
    m_stats.add(fmt::format("num_pum_{}_reqs", m_pum_type_names[i]), s_pum_reqs[i]);
    m_stats.add(fmt::format("num_pum_{}_reqs_served", m_pum_type_names[i]), s_pum_reqs_served[i]);
  }
  m_stats.add("num_pum_reqs", s_pum_total_reqs);
  m_stats.add("num_pum_reqs_served", s_pum_total_reqs_served);
  m_stats.add("pum_queue_len", s_pum_queue_len);
  m_stats.add("pum_occupancy_cycles", s_pum_occupancy_cycles);
}

bool PUMController::send(Request& req) {
  if (!is_pum_type(req.type_id)) {
    return ControllerBase::send(req);
  }

  const auto& spec = *m_device.m_spec;
  if (req.type_id >= static_cast<int>(spec.supported_requests.size())) {
    throw std::runtime_error(
        fmt::format("Request type_id {} is not declared by DRAM standard '{}' (it declares {} types)", req.type_id,
                    spec.standard_name, spec.supported_requests.size()));
  }

  // A PUM request that already names its full destination is not re-mapped.
  if (static_cast<int>(req.addr_vec.size()) < spec.level_count) {
    m_addr_mapper->apply(req);
  }

  for (int level = 1; level < spec.level_count; level++) {
    const int coord = req.addr_vec[level];
    const int size = spec.organization.level_sizes[level];
    if (coord < -1 || (size > 0 && coord >= size)) {
      throw std::runtime_error(fmt::format("PUM request addr_vec[{}] = {} is out of range for level '{}' (size {})",
                                           level, coord, spec.level_names[level], size));
    }
  }
  req.addr_vec[0] = m_channel_id;
  req.final_command = spec.supported_requests[req.type_id];

  req.arrive = m_clk;
  if (!m_pum_buffer.enqueue(req)) {
    req.arrive = -1;
    return false;
  }

  s_pum_reqs[req.type_id - m_first_pum_type_id]++;
  s_pum_total_reqs++;
  return true;
}

void PUMController::retire_pum(ReqBuffer::iterator& req_it) {
  const int type = req_it->type_id - m_first_pum_type_id;
  const Clk_t latency = m_pum_latency[type];
  m_pum_in_flight.push_back({*req_it, m_clk + latency});
  s_pum_occupancy_cycles += latency;
  s_pum_reqs_served[type]++;
  s_pum_total_reqs_served++;
  m_pum_buffer.remove(req_it);
}

void PUMController::serve_completed_pum() {
  // Latencies differ per command, so completions are not in issue order.
  auto done = std::partition(m_pum_in_flight.begin(), m_pum_in_flight.end(),
                             [&](const InFlight& f) { return f.depart > m_clk; });
  for (auto it = done; it != m_pum_in_flight.end(); ++it) {
    if (it->req.callback) {
      it->req.callback(it->req);
    }
  }
  m_pum_in_flight.erase(done, m_pum_in_flight.end());
}

void PUMController::tick() {
  tick_prologue();
  serve_completed_pum();
  s_pum_queue_len += m_pum_buffer.size();

  m_refresh->tick();

  m_rowpolicy->pre_schedule();
  for (auto* p : m_plugins) {
    p->pre_schedule();
  }

  // Priority: active > priority > PUM > read/write
  Candidate cand = pick_best_ready_from(m_active_buffer, {});

  if (!cand.valid) {
    cand = pick_priority_if();
  }

  if (!cand.valid && m_priority_buffer.size() == 0) {
    // A PUM command closes its bank, so it must not close one with an active CPU request.
    cand = pick_best_ready_from(m_pum_buffer, [&](const Request& req) { return !would_close_active(req); });
  }

  if (!cand.valid && m_priority_buffer.size() == 0) {
    cand = pick_rw_if();
  }

  if (cand.valid) {
    const bool is_pum = (cand.buffer == &m_pum_buffer);

    if (!is_pum) {
      m_rowpolicy->try_upgrade_command(*cand.it);
    }

    if (!cand.it->is_stat_updated) {
      update_request_stats(cand.it);
    }

    m_device.issue_command(cand.it->command, cand.it->addr_vec, m_clk);

    m_rowpolicy->on_issue(*cand.it);
    for (auto* p : m_plugins) {
      p->on_issue(*cand.it);
    }

    if (cand.it->command == cand.it->final_command) {
      if (is_pum) {
        retire_pum(cand.it);
      } else {
        retire_request(cand.it, *cand.buffer);
      }
    } else if (m_device.m_spec->command_meta[cand.it->command].is_opening) {
      promote_to_active(cand.it, *cand.buffer);
    }
  }

  m_rowpolicy->post_schedule();
  for (auto* p : m_plugins) {
    p->post_schedule();
  }
}

}  // namespace Ramulator
