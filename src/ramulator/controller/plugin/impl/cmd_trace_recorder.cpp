#include <cstdint>
#include <fmt/format.h>
#include <fstream>
#include <string>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

/// Records every DRAM command issued by the controller to a trace file.
///
/// Two output modes:
///   - **Text** (default): CSV with a self-documenting header line.
///     Each row is: clock, command_name, addr_vec..., type_id, source_id
///   - **Binary**: compact fixed-size records with a self-describing header
///     containing level/command name tables for offline decoding.
///
/// Output files are per-channel: path is suffixed with ".ch0", ".ch1", etc.
///
/// Example config (Python):
///   ramulator.ControllerPlugin.CmdTraceRecorder(path="trace.csv")
///   ramulator.ControllerPlugin.CmdTraceRecorder(path="trace.bin", binary=True)
class CmdTraceRecorder : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, CmdTraceRecorder, "CmdTraceRecorder")

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_path, std::string, "path").required();
    RAMULATOR_PARSE_PARAM(m_binary, bool, "binary").default_val(false);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    const auto& spec = *m_ctrl->m_device.m_spec;
    m_level_count = spec.level_count;

    std::string filepath = fmt::format("{}.ch{}", m_path, m_ctrl->m_channel_id);

    if (m_binary) {
      m_file.open(filepath, std::ios::binary);
      write_binary_header(spec);
    } else {
      m_file.open(filepath);
      write_text_header(spec);
    }
  }

  void on_issue(const Request& req) override {
    if (m_binary) {
      write_binary_record(req);
    } else {
      write_text_record(req);
    }
  }

  void finalize() override {
    if (m_file.is_open()) {
      m_file.close();
    }
  }

 private:
  ControllerBase* m_ctrl = nullptr;
  std::string m_path;
  bool m_binary = false;
  int m_level_count = 0;
  std::ofstream m_file;

  // ── Text mode ──────────────────────────────────────────────────────

  void write_text_header(const DRAMSpec& spec) {
    m_file << "clock,command";
    for (const auto& name : spec.level_names) {
      m_file << "," << name;
    }
    m_file << ",type,source\n";
  }

  void write_text_record(const Request& req) {
    const auto& cmd_names = m_ctrl->m_device.m_spec->command_names;
    m_file << m_ctrl->m_clk << "," << cmd_names[req.command];
    for (int i = 0; i < m_level_count; i++) {
      m_file << "," << req.addr_vec[i];
    }
    m_file << "," << req.type_id << "," << req.source_id << "\n";
  }

  // ── Binary mode ────────────────────────────────────────────────────
  //
  // File layout:
  //   Header:
  //     uint32_t level_count
  //     uint32_t command_count
  //     [level_count null-terminated strings]    (level names)
  //     [command_count null-terminated strings]  (command names)
  //   Records (repeated):
  //     uint64_t clock
  //     int32_t  command_id
  //     int32_t  type_id
  //     int32_t  source_id
  //     int32_t  addr_vec[level_count]

  void write_binary_header(const DRAMSpec& spec) {
    auto write_u32 = [&](uint32_t v) { m_file.write(reinterpret_cast<const char*>(&v), sizeof(v)); };

    write_u32(static_cast<uint32_t>(spec.level_count));
    write_u32(static_cast<uint32_t>(spec.command_count));
    for (const auto& name : spec.level_names) {
      m_file.write(name.c_str(), name.size() + 1);
    }
    for (const auto& name : spec.command_names) {
      m_file.write(name.c_str(), name.size() + 1);
    }
  }

  void write_binary_record(const Request& req) {
    uint64_t clk = m_ctrl->m_clk;
    int32_t cmd = req.command;
    int32_t type = req.type_id;
    int32_t src = req.source_id;

    m_file.write(reinterpret_cast<const char*>(&clk), sizeof(clk));
    m_file.write(reinterpret_cast<const char*>(&cmd), sizeof(cmd));
    m_file.write(reinterpret_cast<const char*>(&type), sizeof(type));
    m_file.write(reinterpret_cast<const char*>(&src), sizeof(src));
    // The header documents addr_vec elements as int32_t, but addr_vec
    // itself is std::vector<int>. sizeof(int) is not guaranteed to equal
    // sizeof(int32_t) — on ILP64 platforms where int is 8 bytes, the
    // old `req.addr_vec.data()` reinterpret-cast with a 4-byte stride
    // captured only the low 4 bytes of every other element and shifted
    // the rest by 4 bytes per element, producing a binary file the
    // documented decoder couldn't read.
    //
    // Copy through int32_t to match the header contract on every
    // platform. This stage runs once per issued command — negligible
    // versus the file write itself.
    std::vector<int32_t> addr_vec32(req.addr_vec.begin(), req.addr_vec.end());
    m_file.write(reinterpret_cast<const char*>(addr_vec32.data()), m_level_count * sizeof(int32_t));
  }
};

}  // namespace Ramulator
