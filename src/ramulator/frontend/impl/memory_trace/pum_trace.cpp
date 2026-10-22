#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ramulator/base/param.h"
#include "ramulator/frontend/i_frontend.h"

namespace Ramulator {

namespace fs = std::filesystem;

// Replays a trace of reads, writes and processing-using-DRAM requests once.
//
// One entry per line; '#' starts a comment:
//   <type> <addr_vec>   type is R, W, or a request type the DRAM standard
//                       declares (e.g. PUM_AAP); addr_vec is comma-separated
//   FENCE               nothing more is sent until every sent request completed
//
// request_types lists the standard's request types in type-id order, and
// level_counts the count of each addr_vec level; both come from the DRAM object.
class PUMTrace : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, PUMTrace, "PUMTrace")

 private:
  static constexpr int kFence = -1;

  struct Entry {
    int type_id;
    AddrVec_t addr_vec;
    Addr_t addr;  // Unique per location, for write forwarding and coalescing
  };
  std::vector<Entry> m_trace;
  size_t m_next = 0;
  size_t m_requests_end = 0;  // One past the last non-FENCE entry
  size_t m_outstanding = 0;

  std::string m_trace_path;
  std::vector<std::string> m_request_types;
  std::vector<int> m_level_counts;

  std::vector<size_t> s_sent;
  std::vector<size_t> s_completed;
  size_t s_cycles = 0;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_clock_ratio, unsigned int, "clock_ratio").required();
    RAMULATOR_PARSE_PARAM(m_trace_path, std::string, "path").required();
    RAMULATOR_PARSE_PARAM(m_request_types, std::vector<std::string>, "request_types").required();
    RAMULATOR_PARSE_PARAM(m_level_counts, std::vector<int>, "level_counts").required();

    m_logger.info(fmt::format("Loading trace file {} ...", m_trace_path));
    init_trace(m_trace_path);
    m_logger.info(fmt::format("Loaded {} lines.", m_trace.size()));

    s_sent.assign(m_request_types.size(), 0);
    s_completed.assign(m_request_types.size(), 0);
    for (size_t i = 0; i < m_request_types.size(); i++) {
      m_stats.add(fmt::format("num_{}_sent", m_request_types[i]), s_sent[i]);
      m_stats.add(fmt::format("num_{}_completed", m_request_types[i]), s_completed[i]);
    }
    m_stats.add("cycles", s_cycles);
  };

  void tick() override {
    m_clk++;

    while (m_next < m_requests_end && m_trace[m_next].type_id == kFence) {
      if (m_outstanding > 0) {
        return;
      }
      m_next++;
    }
    if (m_next >= m_requests_end) {
      return;
    }

    const Entry& e = m_trace[m_next];
    Request req(e.addr_vec, e.type_id);
    req.addr = e.addr;
    req.size_bytes = m_memory_system->get_tx_bytes();
    req.callback = [this](Request& done) {
      m_outstanding--;
      s_completed[done.type_id]++;
      record_end();
    };
    // Counted before sending: a coalesced write completes inside send().
    m_outstanding++;
    if (m_memory_system->send(req)) {
      s_sent[e.type_id]++;
      m_next++;
      record_end();
    } else {
      m_outstanding--;
    }
  };

  bool is_finished() override {
    return m_next >= m_requests_end && m_outstanding == 0;
  };

 private:
  void record_end() {
    if (is_finished()) {
      s_cycles = m_clk;
    }
  }

  void init_trace(const std::string& file_path_str) {
    fs::path trace_path(file_path_str);
    if (!fs::exists(trace_path)) {
      throw std::runtime_error(fmt::format("Trace {} does not exist!", file_path_str));
    }

    std::ifstream trace_file(trace_path);
    if (!trace_file.is_open()) {
      throw std::runtime_error(fmt::format("Trace {} cannot be opened!", file_path_str));
    }

    std::unordered_map<std::string, int> type_ids = {{"R", Request::Type::Read}, {"W", Request::Type::Write}};
    for (size_t i = 0; i < m_request_types.size(); i++) {
      type_ids[m_request_types[i]] = static_cast<int>(i);
    }

    std::string line;
    int line_num = 0;
    while (std::getline(trace_file, line)) {
      line_num++;
      line = line.substr(0, line.find('#'));
      std::vector<std::string> tokens;
      tokenize(tokens, line, " ");
      std::erase(tokens, "");
      if (tokens.empty()) {
        continue;
      }

      if (tokens.size() == 1 && tokens[0] == "FENCE") {
        m_trace.push_back({kFence, {}, -1});
        continue;
      }
      if (tokens.size() != 2) {
        throw std::runtime_error(
            fmt::format("Trace {} line {}: expected '<type> <addr_vec>' or 'FENCE'", file_path_str, line_num));
      }

      auto type_it = type_ids.find(tokens[0]);
      if (type_it == type_ids.end()) {
        throw std::runtime_error(
            fmt::format("Trace {} line {}: unknown request type '{}'", file_path_str, line_num, tokens[0]));
      }

      std::vector<std::string> addr_vec_tokens;
      tokenize(addr_vec_tokens, tokens[1], ",");
      if (addr_vec_tokens.size() != m_level_counts.size()) {
        throw std::runtime_error(fmt::format("Trace {} line {}: addr_vec has {} levels, the DRAM has {}", file_path_str,
                                             line_num, addr_vec_tokens.size(), m_level_counts.size()));
      }

      AddrVec_t addr_vec;
      Addr_t addr = 0;
      for (size_t level = 0; level < addr_vec_tokens.size(); level++) {
        addr_vec.push_back(static_cast<int>(std::stoll(addr_vec_tokens[level])));
        if (level > 0) {
          addr = addr * m_level_counts[level] + addr_vec.back();
        }
      }

      m_trace.push_back({type_it->second, addr_vec, addr});
      m_requests_end = m_trace.size();
    }

    trace_file.close();
  };
};

}  // namespace Ramulator
