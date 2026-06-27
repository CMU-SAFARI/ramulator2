#include "ramulator/frontend/impl/processor/bhO3/bhcore.h"

#include <fmt/format.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#include "ramulator/base/utils.h"
#include "ramulator/frontend/impl/processor/bhO3/bhllc.h"

namespace Ramulator {

namespace fs = std::filesystem;

BHO3Core::Trace::Trace(std::string file_path_str) {
  fs::path trace_path(file_path_str);
  if (!fs::exists(trace_path)) {
    throw std::runtime_error(fmt::format("Trace {} does not exist!", file_path_str));
  }

  std::ifstream trace_file(trace_path);
  if (!trace_file.is_open()) {
    throw std::runtime_error(fmt::format("Trace {} cannot be opened!", file_path_str));
  }

  std::string line;
  while (std::getline(trace_file, line)) {
    std::vector<std::string> tokens;
    tokenize(tokens, line, " ");

    int num_tokens = tokens.size();
    if (num_tokens != 2 && num_tokens != 3) {
      throw std::runtime_error(fmt::format("Trace {} format invalid!", file_path_str));
    }
    int bubble_count = std::stoi(tokens[0]);
    Addr_t load_addr = std::stoll(tokens[1]);

    bool has_store = num_tokens == 2 ? false : true;
    if (has_store) {
      Addr_t store_addr = std::stoll(tokens[2]);
      m_trace.push_back({bubble_count, load_addr, store_addr});
    } else {
      m_trace.push_back({bubble_count, load_addr, -1});
    }
  }

  trace_file.close();
  m_trace_length = m_trace.size();
}

const BHO3Core::Trace::Inst& BHO3Core::Trace::get_next_inst() {
  const Inst& inst = m_trace[m_curr_trace_idx];
  m_curr_trace_idx = (m_curr_trace_idx + 1) % m_trace_length;
  return inst;
}

BHO3Core::InstWindow::InstWindow(int ipc, int depth)
    : m_ipc(ipc), m_depth(depth), m_ready_list(depth, false), m_addr_list(depth, -1) {}

bool BHO3Core::InstWindow::is_full() {
  return m_load == m_depth;
}

void BHO3Core::InstWindow::insert(bool ready, Addr_t addr) {
  m_ready_list.at(m_head_idx) = ready;
  m_addr_list.at(m_head_idx) = addr;

  m_head_idx = (m_head_idx + 1) % m_depth;
  m_load++;
}

int BHO3Core::InstWindow::retire() {
  if (m_load == 0) {
    return 0;
  }

  int num_retired = 0;
  while (m_load > 0 && num_retired < m_ipc) {
    if (!m_ready_list.at(m_tail_idx)) {
      break;
    }

    m_tail_idx = (m_tail_idx + 1) % m_depth;
    m_load--;
    num_retired++;
  }
  return num_retired;
}

void BHO3Core::InstWindow::set_ready(Addr_t addr) {
  if (m_load == 0) {
    return;
  }

  int index = m_tail_idx;
  for (int i = 0; i < m_load; i++) {
    if (m_addr_list[index] == addr) {
      m_ready_list[index] = true;
    }
    index++;
    if (index == m_depth) {
      index = 0;
    }
  }
}

BHO3Core::BHO3Core(const Clk_t& clk, int id, int ipc, int depth, size_t num_expected_insts,
                   uint64_t num_max_cycles, std::string trace_path, ITranslation* translation,
                   BHO3LLC* llc, int lat_hist_sens, std::string dump_path, bool is_attacker)
    : m_clk(clk),
      m_id(id),
      m_window(ipc, depth),
      m_trace(trace_path),
      m_num_expected_insts(num_expected_insts),
      m_num_max_cycles(num_max_cycles),
      m_translation(translation),
      m_llc(llc),
      m_is_attacker(is_attacker),
      m_lat_hist_sens(lat_hist_sens),
      m_dump_path(std::move(dump_path)) {
  auto inst = m_trace.get_next_inst();
  m_num_bubbles = inst.bubble_count;
  m_load_addr = inst.load_addr;
  m_writeback_addr = inst.store_addr;
}

void BHO3Core::tick() {
  size_t retired = m_window.retire();
  s_insts_retired += retired;
  if (!reached_expected_num_insts) {
    s_insts_recorded += retired;
    if (s_insts_retired >= m_num_expected_insts) {
      reached_expected_num_insts = true;
      s_cycles_recorded = m_clk;
    }
  }
  // Hard cycle cap: if the core has been running longer than configured,
  // stop fetching new instructions. The frontend treats reached_max_cycles
  // as a finished signal alongside reached_expected_num_insts.
  if (!reached_max_cycles && m_num_max_cycles > 0 &&
      static_cast<uint64_t>(m_clk) >= m_num_max_cycles) {
    reached_max_cycles = true;
  }
  if (reached_max_cycles) {
    return;
  }

  // Issue non-memory instructions first
  int num_inserted_insts = 0;
  while (m_num_bubbles > 0) {
    if (num_inserted_insts == m_window.m_ipc) {
      return;
    }
    if (m_window.is_full()) {
      return;
    }
    m_window.insert(true, -1);
    num_inserted_insts++;
    m_num_bubbles--;
  }

  // Send the load to the LLC
  if (m_load_addr != -1) {
    if (num_inserted_insts == m_window.m_ipc) {
      return;
    }
    if (m_window.is_full()) {
      return;
    }

    Request load_request(m_load_addr, Request::Type::Read, m_id, m_callback);
    if (!m_translation->translate(load_request)) {
      return;
    }

    if (m_llc->send(load_request)) {
      m_window.insert(false, load_request.addr);
      m_load_addr = -1;
      s_mem_requests_issued++;
      if (m_writeback_addr != -1) {
        return;
      }
    } else {
      // Send rejected (cache miss with no MSHR / line, or BlockHammer
      // blacklist cap reached). Stall — try again next tick.
      return;
    }
  }

  // Send writeback to the LLC
  if (m_writeback_addr != -1) {
    Request writeback_request(m_writeback_addr, Request::Type::Write, m_id, m_callback);
    if (!m_translation->translate(writeback_request)) {
      return;
    }
    if (!m_llc->send(writeback_request)) {
      return;
    }
    s_mem_requests_issued++;
  }

  auto inst = m_trace.get_next_inst();
  m_num_bubbles = inst.bubble_count;
  m_load_addr = inst.load_addr;
  m_writeback_addr = inst.store_addr;
}

void BHO3Core::receive(Request& req) {
  m_window.set_ready(req.addr);

  if (req.arrive != -1 && req.depart > m_last_mem_cycle) {
    if (!reached_expected_num_insts) {
      s_mem_access_cycles += (req.depart - std::max(m_last_mem_cycle, req.arrive));
      m_last_mem_cycle = req.depart;
    }
    // Bucket the request latency for the per-core latency histogram.
    // m_lat_hist_sens is the cycles-per-bucket; lat_hist_sens=0 disables.
    if (m_lat_hist_sens > 0) {
      Clk_t latency = req.depart - req.arrive;
      int bucket = static_cast<int>(latency) / m_lat_hist_sens;
      m_lat_histogram[bucket]++;
    }
  }
}

void BHO3Core::dump_histogram() const {
  if (m_dump_path.empty() || m_lat_hist_sens <= 0 || m_lat_histogram.empty()) {
    return;
  }

  fs::path dir(m_dump_path);
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    return;  // best-effort; if the dir can't be created we just skip
  }

  fs::path out = dir / fmt::format("core_{}_lat_hist.csv", m_id);
  std::ofstream f(out);
  if (!f.is_open()) {
    return;
  }

  f << "bucket_low_cycles,bucket_high_cycles,count,is_attacker\n";
  // Iterate buckets in ascending order for a clean CSV.
  std::vector<std::pair<int, uint64_t>> sorted(m_lat_histogram.begin(), m_lat_histogram.end());
  std::sort(sorted.begin(), sorted.end());
  for (const auto& [bucket, count] : sorted) {
    int low = bucket * m_lat_hist_sens;
    int high = low + m_lat_hist_sens - 1;
    f << low << "," << high << "," << count << "," << (m_is_attacker ? 1 : 0) << "\n";
  }
}

}  // namespace Ramulator
