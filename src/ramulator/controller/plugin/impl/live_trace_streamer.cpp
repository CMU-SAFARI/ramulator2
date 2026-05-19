/**
 * LiveTraceStreamer — streams DRAM command events to the visualizer in real time.
 *
 * A controller plugin that buffers issued commands and periodically flushes
 * them as JSON over HTTP POST to the Nuxt visualizer server, which relays
 * them to connected browser clients via WebSocket.
 *
 * Config (Python):
 *   ramulator.controller_plugin.LiveTraceStreamer(
 *       port=3000,                # visualizer server port
 *       tick_interval=10000,      # flush every N DRAM ticks
 *       update_interval_s=0.5,    # OR every N real-world seconds
 *       dram_type="DDR5",         # label shown in visualizer header
 *   )
 *
 * Flush fires when EITHER threshold is reached (whichever comes first).
 * At finalize() any remaining buffered events are flushed and a "done"
 * message is sent so the browser builds and renders the full trace.
 */

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fmt/format.h>

#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

class LiveTraceStreamer : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, LiveTraceStreamer, "LiveTraceStreamer")

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_port, int, "port").default_val(3000);
    RAMULATOR_PARSE_PARAM(m_tick_interval, int, "tick_interval").default_val(10000);
    RAMULATOR_PARSE_PARAM(m_update_interval_s, double, "update_interval_s").default_val(0.5);
    RAMULATOR_PARSE_PARAM(m_dram_type, std::string, "dram_type").default_val(std::string(""));
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    const auto& spec = *m_ctrl->m_device.m_spec;
    m_level_count = spec.level_count;
    m_addr_bufs.resize(m_level_count);

    send_init(spec);
    m_last_flush_time = clock::now();
    m_last_flush_tick = m_ctrl->m_clk;
  }

  void on_issue(const Request& req) override {
    if (m_interrupted) return;
    m_clk_buf.push_back(m_ctrl->m_clk);
    m_arrive_buf.push_back(req.arrive);
    m_cmd_buf.push_back(static_cast<uint8_t>(req.command));
    m_type_buf.push_back(static_cast<int8_t>(req.type_id));
    m_source_buf.push_back(static_cast<int16_t>(req.source_id));
    for (int k = 0; k < m_level_count; k++) {
      m_addr_bufs[k].push_back(static_cast<int32_t>(req.addr_vec[k]));
    }
  }

  void post_schedule() override {
    if (m_clk_buf.empty()) return;

    auto now = clock::now();
    double elapsed_s = std::chrono::duration<double>(now - m_last_flush_time).count();
    int64_t elapsed_ticks = m_ctrl->m_clk - m_last_flush_tick;

    if (elapsed_ticks >= m_tick_interval || elapsed_s >= m_update_interval_s) {
      if (check_interrupted()) {
        fmt::print("[LiveTraceStreamer] Visualization interrupted by user. Stopping stream.\n");
        m_interrupted = true;
        clear_buffers();
        return;
      }
      flush_events();
      m_last_flush_time = now;
      m_last_flush_tick = m_ctrl->m_clk;
    }
  }

  void finalize() override {
    if (!m_interrupted) {
      if (!m_clk_buf.empty()) flush_events();
      http_post(R"({"type":"done"})");
    }

    clear_buffers();
    m_addr_bufs = {};
  }

 private:
  using clock = std::chrono::steady_clock;

  ControllerBase* m_ctrl = nullptr;
  int m_port = 3000;
  int m_tick_interval = 10000;
  double m_update_interval_s = 0.5;
  std::string m_dram_type;
  int m_level_count = 0;
  bool m_interrupted = false;

  std::vector<int64_t> m_clk_buf;
  std::vector<int64_t> m_arrive_buf;
  std::vector<uint8_t> m_cmd_buf;
  std::vector<int8_t> m_type_buf;
  std::vector<int16_t> m_source_buf;
  std::vector<std::vector<int32_t>> m_addr_bufs;

  clock::time_point m_last_flush_time;
  int64_t m_last_flush_tick = 0;

  // ── JSON helpers ──────────────────────────────────────────────────

  static std::string json_str_array(const std::vector<std::string>& v) {
    std::string r = "[";
    for (size_t i = 0; i < v.size(); i++) {
      if (i) r += ',';
      r += '"';
      r += v[i];
      r += '"';
    }
    return r + ']';
  }

  static std::string json_int_array(const std::vector<int>& v) {
    std::string r = "[";
    for (size_t i = 0; i < v.size(); i++) {
      if (i) r += ',';
      r += std::to_string(v[i]);
    }
    return r + ']';
  }

  static std::string json_cmd_meta(const std::vector<DRAMCommandMeta>& meta) {
    std::string r = "[";
    for (size_t i = 0; i < meta.size(); i++) {
      if (i) r += ',';
      const auto& m = meta[i];
      r += fmt::format(
          R"({{"isOpening":{},"isClosing":{},"isAccessing":{},"isRefreshing":{},"isRowCommand":{},"isColumnCommand":{}}})",
          m.is_opening ? "true" : "false", m.is_closing ? "true" : "false",
          m.is_accessing ? "true" : "false", m.is_refreshing ? "true" : "false",
          m.is_row_command ? "true" : "false", m.is_column_command ? "true" : "false");
    }
    return r + ']';
  }

  // ── Protocol messages ─────────────────────────────────────────────

  void send_init(const DRAMSpec& spec) {
    auto body = fmt::format(
        R"({{"type":"init",)"
        R"("header":{{"version":[1,1],)"
        R"("levelCount":{},"commandCount":{},"timingCount":{},)"
        R"("channelWidth":{},"prefetchSize":{},"dq":{},)"
        R"("channelId":{},"readLatency":{},"dramType":"{}"}},)"
        R"("spec":{{"levelNames":{},"levelSizes":{},)"
        R"("commandNames":{},"commandMeta":{},"commandCycles":{},)"
        R"("timingNames":{},"timingValues":{}}}}})",
        spec.level_count, spec.command_count, spec.timing_count,
        spec.channel_width, spec.internal_prefetch_size, spec.organization.dq,
        m_ctrl->m_channel_id, spec.read_latency, m_dram_type,
        json_str_array(spec.level_names), json_int_array(spec.organization.level_sizes),
        json_str_array(spec.command_names), json_cmd_meta(spec.command_meta),
        json_int_array(spec.command_cycles),
        json_str_array(spec.timing_names), json_int_array(spec.timing_vals));
    http_post(body);
  }

  void flush_events() {
    size_t n = m_clk_buf.size();
    if (n == 0) return;

    std::string events;
    events.reserve(n * 80);
    events = "[";
    for (size_t i = 0; i < n; i++) {
      if (i) events += ',';
      events += fmt::format(
          R"({{"clk":{},"arrive":{},"cmdId":{},"typeId":{},"sourceId":{},"addr":[)",
          m_clk_buf[i], m_arrive_buf[i], m_cmd_buf[i], m_type_buf[i], m_source_buf[i]);
      for (int k = 0; k < m_level_count; k++) {
        if (k) events += ',';
        events += std::to_string(m_addr_bufs[k][i]);
      }
      events += "]}";
    }
    events += ']';

    http_post(fmt::format(R"({{"type":"events","events":{}}})", events));

    clear_buffers();
  }

  void clear_buffers() {
    m_clk_buf.clear();
    m_arrive_buf.clear();
    m_cmd_buf.clear();
    m_type_buf.clear();
    m_source_buf.clear();
    for (auto& buf : m_addr_bufs) buf.clear();
  }

  // ── Minimal HTTP POST to localhost ────────────────────────────────

  bool check_interrupted() {
    std::string response;
    http_post(R"({"type":"status"})", &response);
    return response.find("\"interrupted\":true") != std::string::npos;
  }

  void http_post(const std::string& body, std::string* out_response = nullptr) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(m_port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
      ::close(fd);
      return;
    }

    auto request = fmt::format(
        "POST /api/live-trace-push HTTP/1.1\r\n"
        "Host: localhost:{}\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: {}\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{}",
        m_port, body.size(), body);

    ::send(fd, request.data(), request.size(), MSG_NOSIGNAL);

    char buf[512];
    int bytes_read;
    while ((bytes_read = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
      if (out_response) {
        out_response->append(buf, bytes_read);
      }
    }

    ::close(fd);
  }
};

}  // namespace Ramulator
