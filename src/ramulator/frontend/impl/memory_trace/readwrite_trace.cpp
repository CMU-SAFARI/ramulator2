#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iostream>

#include "ramulator/base/param.h"
#include "ramulator/frontend/i_frontend.h"

namespace Ramulator {

namespace fs = std::filesystem;

class ReadWriteTrace : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, ReadWriteTrace, "ReadWriteTrace")

 private:
  struct Trace {
    bool is_write;
    AddrVec_t addr_vec;
  };
  std::vector<Trace> m_trace;

  size_t m_trace_length = 0;
  size_t m_curr_trace_idx = 0;
  size_t m_trace_count = 0;
  std::string m_trace_path;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_clock_ratio, unsigned int, "clock_ratio").required();
    RAMULATOR_PARSE_PARAM(m_trace_path, std::string, "path").required();

    m_logger.info(fmt::format("Loading trace file {} ...", m_trace_path));
    init_trace(m_trace_path);
    m_logger.info(fmt::format("Loaded {} lines.", m_trace.size()));
  };

  void tick() override {
    const Trace& t = m_trace[m_curr_trace_idx];
    Request req(t.addr_vec, t.is_write ? Request::Type::Write : Request::Type::Read);
    req.size_bytes = m_memory_system->get_tx_bytes();
    bool sent = m_memory_system->send(req);
    if (sent) {
      m_curr_trace_idx = (m_curr_trace_idx + 1) % m_trace_length;
      m_trace_count++;
    }
  };

 private:
  // Trace format: one memory access per line, space-separated.
  //   <op> <addr_vec>
  //
  // - op:       R (read) or W (write)
  // - addr_vec: comma-separated integers forming a multi-dimensional address
  //             vector (e.g., channel,rank,bank,row,column)
  //
  // Example:
  //   R 0,1,2,100,32
  //   W 0,0,3,200,16
  //
  // The trace replays cyclically.
  void init_trace(const std::string& file_path_str) {
    fs::path trace_path(file_path_str);
    if (!fs::exists(trace_path)) {
      throw std::runtime_error(fmt::format("Trace {} does not exist!", file_path_str));
    }

    std::ifstream trace_file(trace_path);
    if (!trace_file.is_open()) {
      throw std::runtime_error(fmt::format("Trace {} cannot be opened!", file_path_str));
    }

    std::string line;
    int line_num = 0;
    while (std::getline(trace_file, line)) {
      line_num++;
      std::vector<std::string> tokens;
      tokenize(tokens, line, " ");

      if (tokens.size() != 2) {
        throw std::runtime_error(
            fmt::format("Trace {} line {}: expected 2 tokens, got {}", file_path_str, line_num, tokens.size()));
      }

      bool is_write = false;
      if (tokens[0] == "R") {
        is_write = false;
      } else if (tokens[0] == "W") {
        is_write = true;
      } else {
        throw std::runtime_error(
            fmt::format("Trace {} line {}: unknown type '{}' (expected R or W)", file_path_str, line_num, tokens[0]));
      }

      std::vector<std::string> addr_vec_tokens;
      tokenize(addr_vec_tokens, tokens[1], ",");

      AddrVec_t addr_vec;
      for (const auto& token : addr_vec_tokens) {
        addr_vec.push_back(static_cast<int>(std::stoll(token)));
      }

      m_trace.push_back({is_write, addr_vec});
    }

    trace_file.close();

    m_trace_length = m_trace.size();
    if (m_trace_length == 0) {
      throw std::runtime_error(
          fmt::format("Trace {} produced no entries — file is empty", file_path_str));
    }
  };

  bool is_finished() override {
    return m_trace_count >= m_trace_length;
  };
};

}  // namespace Ramulator