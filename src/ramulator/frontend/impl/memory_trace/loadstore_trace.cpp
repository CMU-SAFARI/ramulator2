#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iostream>

#include "ramulator/base/param.h"
#include "ramulator/frontend/i_frontend.h"

namespace Ramulator {

namespace fs = std::filesystem;

class LoadStoreTrace : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, LoadStoreTrace, "LoadStoreTrace")

 private:
  struct Trace {
    bool is_write;
    Addr_t addr;
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
    bool request_sent = m_memory_system->send({t.addr, t.is_write ? Request::Type::Write : Request::Type::Read});
    if (request_sent) {
      m_curr_trace_idx = (m_curr_trace_idx + 1) % m_trace_length;
      m_trace_count++;
    }
  };

 private:
  // Trace format: one memory access per line, space-separated.
  //   <op> <address>
  //
  // - op:      LD (read) or ST (write)
  // - address: memory address (decimal or 0x hex)
  //
  // Example:
  //   LD 0x12340
  //   ST 4096
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
      if (tokens[0] == "LD") {
        is_write = false;
      } else if (tokens[0] == "ST") {
        is_write = true;
      } else {
        throw std::runtime_error(
            fmt::format("Trace {} line {}: unknown type '{}' (expected LD or ST)", file_path_str, line_num, tokens[0]));
      }

      Addr_t addr = -1;
      if (tokens[1].compare(0, 2, "0x") == 0 || tokens[1].compare(0, 2, "0X") == 0) {
        addr = std::stoll(tokens[1].substr(2), nullptr, 16);
      } else {
        addr = std::stoll(tokens[1]);
      }
      m_trace.push_back({is_write, addr});
    }

    trace_file.close();

    m_trace_length = m_trace.size();
  };

  bool is_finished() override {
    return m_trace_count >= m_trace_length;
  };
};

}  // namespace Ramulator