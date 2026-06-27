/*
 * RAM2BIN v1.1 — Ramulator2 Binary Command Trace
 * ==============================================
 *
 * A self-describing, Structure-of-Arrays binary trace format for DRAM
 * command events. Designed for mmap + zero-copy WebGL upload.
 *
 * FILE LAYOUT
 * ===========
 *
 * +-----------------------------+
 * | Header          (64 bytes)  |
 * +-----------------------------+
 * | Spec Section    (variable)  |  <- level/cmd/timing tables
 * +-----------------------------+
 * | padding         (0-7 bytes) |  <- align data_offset to 8 bytes
 * +=============================+
 * | clk[]        N x int64      |
 * +-----------------------------+
 * | arrive[]     N x int64      |
 * +-----------------------------+
 * | cmd_id[]     N x uint8      |
 * +-----------------------------+
 * | type_id[]    N x int8       |
 * +-----------------------------+
 * | source_id[]  N x int16      |
 * +-----------------------------+
 * | addr[0][]    N x int32      |  <- first level  (e.g. Channel)
 * +-----------------------------+
 * | addr[1][]    N x int32      |  <- second level (e.g. Rank)
 * +-----------------------------+
 * | ...                         |
 * +-----------------------------+
 * | addr[L-1][]  N x int32      |  <- last level   (e.g. Column)
 * +-----------------------------+
 *
 * N = num_entries, L = level_count.
 * Total data size = N * (8 + 8 + 1 + 1 + 2 + 4*L) = N * (20 + 4*L).
 *
 * HEADER (64 bytes)
 * =================
 *
 * +------------------+--------+--------+-----------------------------------------------+
 * | Field            | Offset | Size   | Description                                   |
 * +------------------+--------+--------+-----------------------------------------------+
 * | magic            |  0     |  8B    | "RAM2BIN\0" (null-terminated, zero-padded)    |
 * | version          |  8     |  2B    | [major, minor] file format version            |
 * | flags            | 10     |  2B    | Bit flags (reserved, must be 0)               |
 * | level_count      | 12     |  2B    | uint16  Number of hierarchy levels            |
 * | command_count    | 14     |  2B    | uint16  Number of command types               |
 * | timing_count     | 16     |  2B    | uint16  Number of timing parameters           |
 * | channel_width    | 18     |  2B    | uint16  Channel width in bits (e.g. 64)       |
 * | prefetch_size    | 20     |  2B    | uint16  Internal prefetch size (e.g. 8)       |
 * | dq               | 22     |  2B    | uint16  DQ width of the device (e.g. 8)       |
 * | channel_id       | 24     |  4B    | uint32  Channel index this file records       |
 * | read_latency     | 28     |  4B    | int32   Precomputed read latency (cycles)     |
 * | num_entries      | 32     |  8B    | uint64  Number of trace entries (= N)         |
 * | data_offset      | 40     |  8B    | uint64  Byte offset where SoA arrays begin    |
 * | dram_type        | 48     | 16B    | char[16] DRAM standard (e.g. "DDR4\0...\0")   |
 * +------------------+--------+--------+-----------------------------------------------+
 *
 * SPEC SECTION (variable length, starts at offset 64)
 * ===================================================
 *
 * Written sequentially in this order:
 *
 * 1. Level names       
 * level_count x null-terminated ASCII strings
 * e.g. "Channel\0" "Rank\0" "BankGroup\0" ...
 *
 * 2. Level sizes
 * level_count x uint32
 * Organization count per level from the spec.
 * e.g. [1, 2, 4, 4, 65536, 1024] for a 2-rank DDR4
 * with 4 BG, 4 banks/BG, 64K rows, 1K columns.
 *
 * 3. Command names
 * command_count x null-terminated ASCII strings
 * e.g. "ACT\0" "PREpb\0" "PREab\0" "RD\0" ...
 *
 * 4. Command meta
 * command_count x uint8 bitfield
 * bit 0: is_opening (e.g. ACT)
 * bit 1: is_closing (e.g. PREpb, PREab)
 * bit 2: is_accessing (e.g. RD, WR, RDA, WRA)
 * bit 3: is_refreshing (e.g. REFab)
 * bit 4: is_row_command
 * bit 5: is_column_command
 * bits 6-7: reserved (0)
 *
 * 5. Command cycles
 * command_count x uint8
 * CA bus cycle count per command, aligned with command names.
 * Defaults to 1 for commands without a Python spec override.
 *
 * 6. Timing names
 * timing_count x null-terminated ASCII strings
 * e.g. "rate\0" "nBL\0" "nCL\0" "nRCD\0" ...
 *
 * 7. Timing values
 * timing_count x int32
 * Concrete values matching timing_names order.
 * e.g. [2400, 4, 16, 16, 16, 39, 55, ...] for DDR4_2400R
 *
 * 8. Padding
 * 0-7 zero bytes to align data_offset to 8 bytes.
 *
 * DATA ARRAYS (SoA, starts at data_offset)
 * ========================================
 *
 * Arrays are written contiguously in this fixed order.
 * The reader computes each array's byte offset from data_offset, N, and L:
 *
 * +---------------+-----------+---------------------------------------------------+
 * | Array         | Elem size | Byte range [start, end)                           |
 * +---------------+-----------+---------------------------------------------------+
 * | clk[]         |  8B int64 | [D,             D + 8N)                           |
 * | arrive[]      |  8B int64 | [D + 8N,        D + 16N)                          |
 * | cmd_id[]      |  1B uint8 | [D + 16N,       D + 17N)                          |
 * | type_id[]     |  1B int8  | [D + 17N,       D + 18N)                          |
 * | source_id[]   |  2B int16 | [D + 18N,       D + 20N)                          |
 * | addr[0][]     |  4B int32 | [D + 20N,       D + 24N)                          |
 * | addr[1][]     |  4B int32 | [D + 24N,       D + 28N)                          |
 * | ...           |           |                                                   |
 * | addr[k][]     |  4B int32 | [D + (20+4k)N,  D + (24+4k)N)                     |
 * | ...           |           |                                                   |
 * | addr[L-1][]   |  4B int32 | [D + (20+4(L-1))N,  D + (20+4L)N)                 |
 * +---------------+-----------+---------------------------------------------------+
 *
 * D = data_offset, N = num_entries, L = level_count
 *
 * FIELD DESCRIPTIONS:
 * clk[i]       — Clock cycle when command i was issued.
 * arrive[i]    — Clock cycle when the owning request arrived at the
 *                controller. -1 for maintenance commands (refresh, etc.)
 *                Also serves as a request grouping key: all commands
 *                issued for the same external request share an arrive value.
 * cmd_id[i]    — Command index into the command name table (spec section).
 * type_id[i]   — Request type: 0 = Read, 1 = Write, -1 = internal.
 * source_id[i] — Source / core ID. -1 if not applicable.
 * addr[k][i]   — Address component for hierarchy level k. -1 if the level does not apply to this command.
 *
 * NOTES
 * =====
 * - All integers are little-endian.
 * - All strings are ASCII, null-terminated.
 * - Reserved / flag fields must be zero for v1.
 * - One file per channel; path is suffixed with ".ch0", ".ch1", etc.
 * - Total file size = data_offset + N * (20 + 4*L).
 */

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

/// Records every DRAM command to a binary SoA trace file (RAM2BIN v1).
///
/// Buffers per-field arrays during simulation and flushes them in
/// Structure-of-Arrays order at finalize().  One file per channel.
///
/// Config (Python):
///   ramulator.ControllerPlugin.BinTraceRecorder(path="trace.bin")
class BinTraceRecorder : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, BinTraceRecorder, "BinTraceRecorder")

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_path, std::string, "path").required();
    RAMULATOR_PARSE_PARAM(m_dram_type, std::string, "dram_type").default_val(std::string(""));
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    const auto& spec = *m_ctrl->m_device.m_spec;
    m_level_count = spec.level_count;
    m_addr_bufs.resize(m_level_count);

    std::string filepath = fmt::format("{}.ch{}.ram2bin", m_path, m_ctrl->m_channel_id);
    m_file.open(filepath, std::ios::binary);
    if (!m_file.is_open()) {
      throw std::runtime_error(fmt::format("BinTraceRecorder: failed to open {}", filepath));
    }

    write_header(spec);
    write_spec_section(spec);
    pad_to_8();
    m_data_offset = static_cast<uint64_t>(m_file.tellp());
  }

  void on_issue(const Request& req) override {
    m_clk_buf.push_back(m_ctrl->m_clk);
    m_arrive_buf.push_back(req.arrive);
    m_cmd_buf.push_back(static_cast<uint8_t>(req.command));
    m_type_buf.push_back(static_cast<int8_t>(req.type_id));
    m_source_buf.push_back(static_cast<int16_t>(req.source_id));
    for (int k = 0; k < m_level_count; k++) {
      m_addr_bufs[k].push_back(static_cast<int32_t>(req.addr_vec[k]));
    }
  }

  void finalize() override {
    if (!m_file.is_open()) return;

    flush_arrays();
    patch_header();
    m_file.close();

    m_clk_buf = {};
    m_arrive_buf = {};
    m_cmd_buf = {};
    m_type_buf = {};
    m_source_buf = {};
    m_addr_bufs = {};
  }

 private:
  ControllerBase* m_ctrl = nullptr;
  std::string m_path;
  std::string m_dram_type;
  int m_level_count = 0;
  std::ofstream m_file;
  uint64_t m_data_offset = 0;

  std::vector<int64_t>  m_clk_buf;
  std::vector<int64_t>  m_arrive_buf;
  std::vector<uint8_t>  m_cmd_buf;
  std::vector<int8_t>   m_type_buf;
  std::vector<int16_t>  m_source_buf;
  std::vector<std::vector<int32_t>> m_addr_bufs;

  // ── Header ────────────────────────────────────────────────────────

#pragma pack(push, 1)
  struct Header {
    char     magic[8];        //  0
    uint8_t  version[2];      //  8
    uint16_t flags;           // 10
    uint16_t level_count;     // 12
    uint16_t command_count;   // 14
    uint16_t timing_count;    // 16
    uint16_t channel_width;   // 18
    uint16_t prefetch_size;   // 20
    uint16_t dq;              // 22
    uint32_t channel_id;      // 24
    int32_t  read_latency;    // 28
    uint64_t num_entries;     // 32
    uint64_t data_offset;     // 40
    char     dram_type[16];   // 48
  };
#pragma pack(pop)
  static_assert(sizeof(Header) == 64, "Header must be exactly 64 bytes");

  void write_header(const DRAMSpec& spec) {
    Header h{};
    std::memcpy(h.magic, "RAM2BIN", 8);
    h.version[0]    = 1;
    h.version[1]    = 1;
    h.level_count   = static_cast<uint16_t>(spec.level_count);
    h.command_count = static_cast<uint16_t>(spec.command_count);
    h.timing_count  = static_cast<uint16_t>(spec.timing_count);
    h.channel_width = static_cast<uint16_t>(spec.channel_width);
    h.prefetch_size = static_cast<uint16_t>(spec.internal_prefetch_size);
    h.dq            = static_cast<uint16_t>(spec.organization.dq);
    h.channel_id    = static_cast<uint32_t>(m_ctrl->m_channel_id);
    h.read_latency  = static_cast<int32_t>(spec.read_latency);

    std::memcpy(h.dram_type, m_dram_type.c_str(),
                std::min(m_dram_type.size(), sizeof(h.dram_type) - 1));

    m_file.write(reinterpret_cast<const char*>(&h), sizeof(h));
  }

  void patch_header() {
    uint64_t num_entries = m_clk_buf.size();
    m_file.seekp(32);
    m_file.write(reinterpret_cast<const char*>(&num_entries), sizeof(num_entries));
    m_file.write(reinterpret_cast<const char*>(&m_data_offset), sizeof(m_data_offset));
  }

  // ── Spec section ──────────────────────────────────────────────────

  void write_spec_section(const DRAMSpec& spec) {
    for (const auto& name : spec.level_names)
      write_cstr(name);

    for (int sz : spec.organization.level_sizes)
      write_val(static_cast<uint32_t>(sz));

    for (const auto& name : spec.command_names)
      write_cstr(name);

    for (const auto& meta : spec.command_meta) {
      uint8_t bits = 0;
      if (meta.is_opening)        bits |= 1u << 0;
      if (meta.is_closing)        bits |= 1u << 1;
      if (meta.is_accessing)      bits |= 1u << 2;
      if (meta.is_refreshing)     bits |= 1u << 3;
      if (meta.is_row_command)    bits |= 1u << 4;
      if (meta.is_column_command) bits |= 1u << 5;
      write_val(bits);
    }

    for (int c : spec.command_cycles)
      write_val(static_cast<uint8_t>(c));

    for (const auto& name : spec.timing_names)
      write_cstr(name);

    for (int val : spec.timing_vals)
      write_val(static_cast<int32_t>(val));
  }

  // ── Data arrays ───────────────────────────────────────────────────

  void flush_arrays() {
    write_array(m_clk_buf);
    write_array(m_arrive_buf);
    write_array(m_cmd_buf);
    write_array(m_type_buf);
    write_array(m_source_buf);
    for (auto& buf : m_addr_bufs)
      write_array(buf);
  }

  // ── I/O helpers ───────────────────────────────────────────────────

  void write_cstr(const std::string& s) {
    m_file.write(s.c_str(), static_cast<std::streamsize>(s.size() + 1));
  }

  template <typename T>
  void write_val(T v) {
    m_file.write(reinterpret_cast<const char*>(&v), sizeof(v));
  }

  template <typename T>
  void write_array(const std::vector<T>& v) {
    if (!v.empty())
      m_file.write(reinterpret_cast<const char*>(v.data()),
                   static_cast<std::streamsize>(v.size() * sizeof(T)));
  }

  void pad_to_8() {
    auto pos = static_cast<uint64_t>(m_file.tellp());
    if (uint64_t rem = pos & 7u) {
      static constexpr char zeros[7]{};
      m_file.write(zeros, static_cast<std::streamsize>(8 - rem));
    }
  }
};

}  // namespace Ramulator