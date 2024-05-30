#include <filesystem>
#include <iostream>
#include <fstream>

#include "frontend/frontend.h"
#include "base/exception.h"

namespace Ramulator {

namespace fs = std::filesystem;

class ReadWriteTrace : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, ReadWriteTrace, "ReadWriteTrace", "Read/Write DRAM address vector trace.")

  private:
    struct Trace {
      bool is_write;
      AddrVec_t addr_vec;
    };
    std::vector<Trace> m_trace;

    size_t m_trace_length = 0;
    size_t m_curr_trace_idx = 0;

    Logger_t m_logger;

  public:
    void init() override {
      std::string trace_path_str = param<std::string>("path").desc("Path to the load store trace file.").required();
      m_clock_ratio = param<uint>("clock_ratio").required();

      m_logger = Logging::create_logger("ReadWriteTrace");
      m_logger->info("Loading trace file {} ...", trace_path_str);
      init_trace(trace_path_str);
      m_logger->info("Loaded {} lines.", m_trace.size());      
    };


    void tick() override {
      const Trace& t = m_trace[m_curr_trace_idx];
      m_memory_system->send({t.addr_vec, t.is_write ? Request::Type::Write : Request::Type::Read});
      m_curr_trace_idx = (m_curr_trace_idx + 1) % m_trace_length;
    };


  private:
    void init_trace(const std::string& file_path_str) {
      fs::path trace_path(file_path_str);
      if (!fs::exists(trace_path)) {
        throw ConfigurationError("Trace {} does not exist!", file_path_str);
      }

      std::ifstream trace_file(trace_path);
      if (!trace_file.is_open()) {
        throw ConfigurationError("Trace {} cannot be opened!", file_path_str);
      }

      std::string line;
      while (std::getline(trace_file, line)) {
        std::vector<std::string> tokens;
        tokenize(tokens, line, " ");

        // TODO: Add line number here for better error messages
        if (tokens.size() != 2) {
          throw ConfigurationError("Trace {} format invalid!", file_path_str);
        }

        bool is_write = false; 
        if (tokens[0] == "R") {
          is_write = false;
        } else if (tokens[0] == "W") {
          is_write = true;
        } else {
          throw ConfigurationError("Trace {} format invalid!", file_path_str);
        }

        std::vector<std::string> addr_vec_tokens;
        tokenize(addr_vec_tokens, tokens[1], ",");

        AddrVec_t addr_vec;
        for (const auto& token : addr_vec_tokens) {
          addr_vec.push_back(std::stoll(token));
        }

        m_trace.push_back({is_write, addr_vec});
      }

      trace_file.close();

      m_trace_length = m_trace.size();
    };

    // TODO: FIXME
    bool is_finished() override {
      return true; 
    };    
};

}        // namespace Ramulator