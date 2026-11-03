/*
 * RAM2BIN v1.2 — Ramulator2 Binary Command Trace
 * ==============================================
 *
 * A self-describing binary trace format for DRAM command events.  The data
 * is written in chunks, each a Structure-of-Arrays block, so the recorder
 * holds at most one chunk in memory and a run that stops early still leaves
 * every complete chunk readable.
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
 * | Chunk 0                     |  <- starts at data_offset
 * +-----------------------------+
 * | Chunk 1                     |
 * +-----------------------------+
 * | ...                         |
 * +-----------------------------+
 *
 * HEADER (64 bytes)
 * =================
 *
 * +------------------+--------+--------+-----------------------------------------------+
 * | Field            | Offset | Size   | Description                                   |
 * +------------------+--------+--------+-----------------------------------------------+
 * | magic            |  0     |  8B    | "RAM2BIN\0" (null-terminated, zero-padded)    |
 * | version          |  8     |  2B    | [major, minor] file format version            |
 * | flags            | 10     |  2B    | bit 0: complete (set when recording ends)     |
 * | level_count      | 12     |  2B    | uint16  Number of hierarchy levels            |
 * | command_count    | 14     |  2B    | uint16  Number of command types               |
 * | timing_count     | 16     |  2B    | uint16  Number of timing parameters           |
 * | channel_width    | 18     |  2B    | uint16  Channel width in bits (e.g. 64)       |
 * | prefetch_size    | 20     |  2B    | uint16  Internal prefetch size (e.g. 8)       |
 * | dq               | 22     |  2B    | uint16  DQ width of the device (e.g. 8)       |
 * | channel_id       | 24     |  4B    | uint32  Channel index this file records       |
 * | read_latency     | 28     |  4B    | int32   Precomputed read latency (cycles)     |
 * | num_entries      | 32     |  8B    | uint64  Total entries, set when complete      |
 * | data_offset      | 40     |  8B    | uint64  Byte offset of the first chunk        |
 * | dram_type        | 48     | 16B    | char[16] DRAM standard (e.g. "DDR4\0...\0")   |
 * +------------------+--------+--------+-----------------------------------------------+
 *
 * A file without the complete flag comes from a run that stopped before
 * finalize(); its num_entries is 0 and readers count the complete chunks.
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
 * CHUNK (n entries, 1 <= n <= 65536)
 * ==================================
 *
 * +---------------+-----------+---------------------------------------------------+
 * | Field         | Elem size | Byte range [start, end) from the chunk start      |
 * +---------------+-----------+---------------------------------------------------+
 * | n             |  4B uint32| [0,             4)                                |
 * | reserved      |  4B       | [4,             8)    zero                        |
 * | clk[]         |  8B int64 | [8,             8 + 8n)                           |
 * | arrive[]      |  8B int64 | [8 + 8n,        8 + 16n)                          |
 * | req_id[]      |  8B int64 | [8 + 16n,       8 + 24n)                          |
 * | cmd_id[]      |  1B uint8 | [8 + 24n,       8 + 25n)                          |
 * | type_id[]     |  1B int8  | [8 + 25n,       8 + 26n)                          |
 * | source_id[]   |  2B int16 | [8 + 26n,       8 + 28n)                          |
 * | addr[k][]     |  4B int32 | [8 + (28+4k)n,  8 + (32+4k)n)    k = 0 .. L-1     |
 * | padding       |           | 0-7 zero bytes; the next chunk is 8-byte aligned  |
 * +---------------+-----------+---------------------------------------------------+
 *
 * L = level_count.  A chunk takes 8 + n * (28 + 4L) bytes before padding.
 *
 * FIELD DESCRIPTIONS:
 * clk[i]       — Clock cycle when command i was issued.
 * arrive[i]    — Clock cycle when the owning request arrived at the
 *                controller. -1 for maintenance commands (refresh, etc.)
 * req_id[i]    — Request::id of the owning request: all commands issued for
 *                the same request share it.  Unique within the file.
 * cmd_id[i]    — Command index into the command name table (spec section).
 * type_id[i]   — Request type: 0 = Read, 1 = Write, -1 = internal.
 * source_id[i] — Source / core ID. -1 if not applicable.
 * addr[k][i]   — Address component for hierarchy level k. -1 if the level does not apply to this command.
 *
 * NOTES
 * =====
 * - All integers are little-endian.
 * - All strings are ASCII, null-terminated.
 * - One file per channel; path is suffixed with ".ch0", ".ch1", etc.
 * - Versions 1.0 and 1.1 store a single block of arrays at data_offset
 *   (clk, arrive, cmd_id, type_id, source_id, addr; no chunk header and no
 *   req_id) with num_entries entries.  Version 1.0 has no command cycles.
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

/// Records every DRAM command to a binary SoA trace file (RAM2BIN v1.2).
///
/// Buffers up to one chunk of per-field arrays and appends each full chunk to
/// the file.  finalize() writes the last chunk and marks the file complete.
/// One file per channel.
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
    if (m_dram_type.empty()) m_dram_type = spec.standard_name;

    m_filepath = fmt::format("{}.ch{}.ram2bin", m_path, m_ctrl->m_channel_id);
    m_file.open(m_filepath, std::ios::binary);
    if (!m_file.is_open()) {
      throw std::runtime_error(fmt::format("BinTraceRecorder: failed to open {}", m_filepath));
    }

    write_header(spec);
    write_spec_section(spec);
    pad_to_8();
    const auto data_offset = static_cast<uint64_t>(m_file.tellp());
    m_file.seekp(40);
    write_val(data_offset);
    m_file.seekp(static_cast<std::streamoff>(data_offset));
    m_file.flush();
    check_io();

    m_clk_buf.reserve(kChunkEntries);
    m_arrive_buf.reserve(kChunkEntries);
    m_id_buf.reserve(kChunkEntries);
    m_cmd_buf.reserve(kChunkEntries);
    m_type_buf.reserve(kChunkEntries);
    m_source_buf.reserve(kChunkEntries);
    for (auto& buf : m_addr_bufs) buf.reserve(kChunkEntries);
  }

  void on_issue(const Request& req) override {
    m_clk_buf.push_back(m_ctrl->m_clk);
    m_arrive_buf.push_back(req.arrive);
    m_id_buf.push_back(req.id);
    m_cmd_buf.push_back(static_cast<uint8_t>(req.command));
    m_type_buf.push_back(static_cast<int8_t>(req.type_id));
    m_source_buf.push_back(static_cast<int16_t>(req.source_id));
    for (int k = 0; k < m_level_count; k++) {
      m_addr_bufs[k].push_back(static_cast<int32_t>(req.addr_vec[k]));
    }
    if (m_clk_buf.size() == kChunkEntries) write_chunk();
  }

  void finalize() override {
    if (!m_file.is_open()) return;

    write_chunk();
    m_file.seekp(10);
    write_val(kFlagComplete);
    m_file.seekp(32);
    write_val(m_num_entries);
    check_io();
    m_file.close();
    if (m_file.fail()) {
      throw std::runtime_error(fmt::format("BinTraceRecorder: failed to write {}", m_filepath));
    }

    m_clk_buf = {};
    m_arrive_buf = {};
    m_id_buf = {};
    m_cmd_buf = {};
    m_type_buf = {};
    m_source_buf = {};
    m_addr_bufs = {};
  }

 private:
  static constexpr size_t kChunkEntries = 65536;
  static constexpr uint16_t kFlagComplete = 1;

  ControllerBase* m_ctrl = nullptr;
  std::string m_path;
  std::string m_dram_type;
  std::string m_filepath;
  int m_level_count = 0;
  std::ofstream m_file;
  uint64_t m_num_entries = 0;

  std::vector<int64_t>  m_clk_buf;
  std::vector<int64_t>  m_arrive_buf;
  std::vector<int64_t>  m_id_buf;
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
    h.version[1]    = 2;
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

  // ── Data chunks ───────────────────────────────────────────────────

  void write_chunk() {
    const size_t n = m_clk_buf.size();
    if (n == 0) return;

    write_val(static_cast<uint32_t>(n));
    write_val(static_cast<uint32_t>(0));
    write_array(m_clk_buf);
    write_array(m_arrive_buf);
    write_array(m_id_buf);
    write_array(m_cmd_buf);
    write_array(m_type_buf);
    write_array(m_source_buf);
    for (auto& buf : m_addr_bufs)
      write_array(buf);
    pad_to_8();
    m_file.flush();
    check_io();
    m_num_entries += n;

    m_clk_buf.clear();
    m_arrive_buf.clear();
    m_id_buf.clear();
    m_cmd_buf.clear();
    m_type_buf.clear();
    m_source_buf.clear();
    for (auto& buf : m_addr_bufs)
      buf.clear();
  }

  // ── I/O helpers ───────────────────────────────────────────────────

  void check_io() {
    if (!m_file) {
      throw std::runtime_error(fmt::format("BinTraceRecorder: failed to write {}", m_filepath));
    }
  }

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
