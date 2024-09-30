#include <cstdint>
#include <filesystem>
#include <fstream>

#include "frontend/frontend.h"
#include "base/exception.h"

namespace Ramulator {

namespace fs = std::filesystem;

class LoadStoreDataTrace : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, LoadStoreDataTrace, "LoadStoreDataTrace", "Load/Store memory address trace with data.")

  private:
    struct Trace {
      bool is_write;
      Addr_t addr;
      uint8_t* payload;
      int payload_size;
    };
    std::vector<Trace> m_trace;

    size_t m_trace_length = 0;
    size_t m_curr_trace_idx = 0;

    size_t m_trace_count = 0;

    Logger_t m_logger;

  public:
    void init() override {
      std::string trace_path_str = param<std::string>("path").desc("Path to the load store trace file.").required();
      m_clock_ratio = param<uint>("clock_ratio").required();

      m_logger = Logging::create_logger("LoadStoreDataTrace");
      m_logger->info("Loading trace file {} ...", trace_path_str);
      init_trace(trace_path_str);
      m_logger->info("Loaded {} lines.", m_trace.size());
    };


    void tick() override {
      const Trace& t = m_trace[m_curr_trace_idx];
      bool request_sent = m_memory_system->send({t.addr, t.is_write ? Request::Type::Write : Request::Type::Read, t.payload, t.payload_size});
      if (request_sent) {
        m_curr_trace_idx = (m_curr_trace_idx + 1) % m_trace_length;
        m_trace_count++;
      }
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

      int line_count = 0;
      std::string line;
      while (std::getline(trace_file, line)) {
        line_count++;
        std::vector<std::string> tokens;
        tokenize(tokens, line, " ");

        if (tokens.size() < 2) {
          throw ConfigurationError("Trace {} format invalid on line {} (Line has too few tokens)!", file_path_str, line_count);
        }

        bool is_write = false; 
        if (tokens[0] == "LD") {
          if (tokens.size() != 3) {
            for (int i = 0; i < tokens.size(); i++)
              std::cout << i << ": " << tokens[i] << std::endl;
            throw ConfigurationError("Trace {} format invalid on line {} (LD must have 3 tokens, not {})!", file_path_str, line_count, tokens.size());
          }
          is_write = false;
        } else if (tokens[0] == "ST") {
          if (tokens.size() < 4) 
            throw ConfigurationError("Trace {} format invalid on line {} (ST must have more tokens)!", file_path_str, line_count);
          if (stoi(tokens[2]) < 1 && tokens.size() != 3 + stoi(tokens[2]))
            throw ConfigurationError("Trace {} format invalid on line {} (ST has too few tokens)!", file_path_str, line_count);
          is_write = true;
        } else {
          throw ConfigurationError("Trace {} format invalid on line {} (unknown command)!", file_path_str, line_count);
        }

        Addr_t addr = -1;
        if (tokens[1].compare(0, 2, "0x") == 0 | tokens[1].compare(0, 2, "0X") == 0) {
          addr = std::stoll(tokens[1].substr(2), nullptr, 16);
        } else {
          addr = std::stoll(tokens[1]);
        }
        
        uint8_t* payload = nullptr;
        int payload_size = stoi(tokens[2]);
        if (is_write) {
          if (stoi(tokens[2]) >= 0 || stoi(tokens[2]) <= 255) {
            payload = new uint8_t[payload_size];
            for (int i = 0; i < payload_size; i++) {
              payload[i] = (uint8_t) stoi(tokens[3 + i]);
            }
          }
        } 

        m_trace.push_back({is_write, addr, payload, payload_size});
      }

      trace_file.close();

      m_trace_length = m_trace.size();
    };

    // TODO: FIXME
    bool is_finished() override {
      return m_trace_count >= m_trace_length; 
    };
};

}        // namespace Ramulator
