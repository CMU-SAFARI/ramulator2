/**
 * LiveTraceStreamer — streams DRAM command events to the visualizer in real time.
 *
 * A controller plugin that buffers issued commands and periodically posts
 * them over HTTP to the visualizer server, which relays them to the browsers
 * watching this channel.  Each channel of each simulation run is its own
 * session, and the server keeps the events so far for viewers that join late.
 *
 * Config (Python):
 *   ramulator.controller_plugin.LiveTraceStreamer(
 *       host="localhost",         # visualizer server host
 *       port=3000,                # visualizer server port
 *       tick_interval=10000,      # flush every N DRAM ticks
 *       update_interval_s=0.5,    # OR every N real-world seconds
 *       dram_type="DDR5",         # label shown in visualizer header (default: the standard's name)
 *   )
 *
 * Flush fires when EITHER threshold is reached (whichever comes first), or
 * when 100,000 events are buffered.  A sender thread encodes and posts the
 * batches, so the simulation only waits when the visualizer falls behind.
 * Without events, a status message every 2 seconds tells the server that the
 * simulation is still running.  At finalize() the remaining events are sent,
 * followed by "done".
 *
 * Streaming stops, and the simulation continues, when a viewer stops it or
 * when the visualizer cannot be reached or does not answer within 5 seconds.
 *
 * The visualizer needs its access token: the streamer uses
 * RAMULATOR_VISUALIZER_TOKEN, the file named by RAMULATOR_VISUALIZER_TOKEN_FILE
 * (Docker shares one between the containers), or for a visualizer on this
 * machine the token file it keeps
 * (${XDG_CACHE_HOME:-~/.cache}/ramulator/visualizer-<port>.token).
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
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
  ~LiveTraceStreamer() override { stop_sender(); }

  void init() override {
    RAMULATOR_PARSE_PARAM(m_host, std::string, "host").default_val(std::string("localhost"));
    RAMULATOR_PARSE_PARAM(m_port, int, "port").default_val(3000);
    RAMULATOR_PARSE_PARAM(m_tick_interval, int, "tick_interval").default_val(10000);
    RAMULATOR_PARSE_PARAM(m_update_interval_s, double, "update_interval_s").default_val(0.5);
    RAMULATOR_PARSE_PARAM(m_dram_type, std::string, "dram_type").default_val(std::string(""));
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    m_memory_system = memory_system;
    const auto& spec = *m_ctrl->m_device.m_spec;
    m_level_count = spec.level_count;
    m_batch = new_batch();
    if (m_dram_type.empty()) m_dram_type = spec.standard_name;

    std::tie(m_run, m_started_at) = run_of(memory_system);
    m_session = fmt::format("{}:{}", m_run, m_ctrl->m_channel_id);
    m_token = read_token();

    send_init();
    m_last_flush_tick = m_ctrl->m_clk;
    m_last_queued = clock::now();
    if (!m_stopped) m_sender = std::thread([this] { run_sender(); });
  }

  void on_issue(const Request& req) override {
    if (m_stopped.load(std::memory_order_relaxed)) return;
    Batch& b = m_batch;
    b.clk.push_back(m_ctrl->m_clk);
    b.arrive.push_back(req.arrive);
    b.id.push_back(req.id);
    b.cmd.push_back(static_cast<uint8_t>(req.command));
    b.type.push_back(static_cast<int8_t>(req.type_id));
    b.source.push_back(static_cast<int16_t>(req.source_id));
    for (int k = 0; k < m_level_count; k++) {
      b.addr[k].push_back(static_cast<int32_t>(req.addr_vec[k]));
    }
    if (b.clk.size() >= kMaxBatch) flush_events();
  }

  void post_schedule() override {
    // Reading the wall clock every tick would cost more than the rest of this plugin.
    if (m_stopped.load(std::memory_order_relaxed) || (m_ctrl->m_clk & 255) != 0) return;

    double elapsed_s = std::chrono::duration<double>(clock::now() - m_last_queued).count();
    if (!m_batch.clk.empty()) {
      if (m_ctrl->m_clk - m_last_flush_tick >= m_tick_interval || elapsed_s >= m_update_interval_s) flush_events();
    } else if (elapsed_s >= kHeartbeatS) {
      enqueue({Job::Status, {}});
    }
  }

  void finalize() override {
    if (!m_batch.clk.empty()) flush_events();
    enqueue({Job::Done, {}});
    stop_sender();
    run_registry().erase(m_memory_system);
    m_batch = {};
  }

 private:
  using clock = std::chrono::steady_clock;
  static constexpr int kSocketTimeoutS = 5;
  /// The visualizer answers in a few bytes; a longer reply is not from it and is not read.
  static constexpr size_t kMaxReplyBytes = 1 << 20;
  static constexpr double kHeartbeatS = 2.0;
  static constexpr size_t kMaxBatch = 100000;
  /// Batches waiting for the sender; beyond this the simulation waits for the visualizer.
  static constexpr size_t kMaxQueued = 4;

  struct Batch {
    std::vector<int64_t> clk, arrive, id;
    std::vector<uint8_t> cmd;
    std::vector<int8_t> type;
    std::vector<int16_t> source;
    std::vector<std::vector<int32_t>> addr;
  };

  struct Job {
    enum Kind { Events, Status, Done } kind;
    Batch batch;
  };

  ControllerBase* m_ctrl = nullptr;
  IMemorySystem* m_memory_system = nullptr;
  std::string m_host = "localhost";
  int m_port = 3000;
  int m_tick_interval = 10000;
  double m_update_interval_s = 0.5;
  std::string m_dram_type;
  std::string m_run;
  int64_t m_started_at = 0;
  std::string m_session;
  std::string m_token;
  int m_level_count = 0;
  std::atomic<bool> m_stopped{false};  // stopped by a viewer, or the visualizer is unreachable

  // Simulation thread
  Batch m_batch;
  int64_t m_last_flush_tick = 0;
  clock::time_point m_last_queued;

  // Shared with the sender thread
  std::thread m_sender;
  std::mutex m_mutex;
  std::condition_variable m_changed;
  std::deque<Job> m_jobs;
  bool m_closing = false;

  Batch new_batch() const {
    Batch b;
    b.addr.resize(m_level_count);
    return b;
  }

  // ── Session identity ──────────────────────────────────────────────

  /// Run id and start time (ms since the epoch) per memory system, shared by all its channels.
  static std::map<const IMemorySystem*, std::pair<std::string, int64_t>>& run_registry() {
    static std::map<const IMemorySystem*, std::pair<std::string, int64_t>> runs;
    return runs;
  }

  static std::pair<std::string, int64_t> run_of(const IMemorySystem* memory_system) {
    auto& runs = run_registry();
    auto it = runs.find(memory_system);
    if (it != runs.end()) return it->second;
    int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    return runs[memory_system] = {fmt::format("{}-{}", ::getpid(), ms), ms};
  }

  std::string read_token() const {
    if (const char* t = std::getenv("RAMULATOR_VISUALIZER_TOKEN"); t && *t) return t;
    auto first_line = [](const std::string& path) {
      std::ifstream file(path);
      std::string token;
      std::getline(file, token);
      return token;
    };
    if (const char* f = std::getenv("RAMULATOR_VISUALIZER_TOKEN_FILE"); f && *f) return first_line(f);
    if (m_host != "localhost" && m_host != "127.0.0.1" && m_host != "::1") return "";
    std::string dir;
    if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x) {
      dir = x;
    } else if (const char* h = std::getenv("HOME"); h && *h) {
      dir = std::string(h) + "/.cache";
    } else {
      return "";
    }
    return first_line(fmt::format("{}/ramulator/visualizer-{}.token", dir, m_port));
  }

  // ── Sender thread ─────────────────────────────────────────────────

  void flush_events() {
    Batch full = std::exchange(m_batch, new_batch());
    m_last_flush_tick = m_ctrl->m_clk;
    enqueue({Job::Events, std::move(full)});
  }

  /// Hands a job to the sender, waiting while kMaxQueued jobs are pending.
  void enqueue(Job job) {
    m_last_queued = clock::now();
    if (!m_sender.joinable()) return;
    std::unique_lock lock(m_mutex);
    m_changed.wait(lock, [&] { return m_jobs.size() < kMaxQueued || m_stopped; });
    if (m_stopped) return;
    m_jobs.push_back(std::move(job));
    m_changed.notify_all();
  }

  void run_sender() {
    while (true) {
      Job job;
      {
        std::unique_lock lock(m_mutex);
        m_changed.wait(lock, [&] { return !m_jobs.empty() || m_closing; });
        if (m_jobs.empty()) return;
        job = std::move(m_jobs.front());
        m_jobs.pop_front();
      }
      switch (job.kind) {
        case Job::Events:
          handle_reply(post("events", encode(job.batch), fmt::format("&count={}", job.batch.clk.size())));
          break;
        case Job::Status:
          handle_reply(post("status", "{}"));
          break;
        case Job::Done:
          post("done", "{}");
          break;
      }
      std::lock_guard lock(m_mutex);
      if (m_stopped) m_jobs.clear();
      m_changed.notify_all();
    }
  }

  /// Sends what is queued, then ends the sender thread.
  void stop_sender() {
    if (!m_sender.joinable()) return;
    {
      std::lock_guard lock(m_mutex);
      m_closing = true;
    }
    m_changed.notify_all();
    m_sender.join();
  }

  // ── JSON helpers ──────────────────────────────────────────────────

  static std::string json_str(const std::string& s) {
    std::string r = "\"";
    for (char c : s) {
      if (c == '"' || c == '\\') {
        r += '\\';
        r += c;
      } else if (static_cast<unsigned char>(c) < 0x20) {
        r += fmt::format("\\u{:04x}", static_cast<unsigned char>(c));
      } else {
        r += c;
      }
    }
    return r + '"';
  }

  static std::string json_str_array(const std::vector<std::string>& v) {
    std::string r = "[";
    for (size_t i = 0; i < v.size(); i++) {
      if (i) r += ',';
      r += json_str(v[i]);
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

  /// Appends `"name":[f(0),f(1),...],` for n events.
  template <typename F>
  static void append_column(std::string& s, const char* name, size_t n, F value) {
    s += '"';
    s += name;
    s += "\":[";
    for (size_t i = 0; i < n; i++) {
      if (i) s += ',';
      fmt::format_to(std::back_inserter(s), "{}", value(i));
    }
    s += "],";
  }

  // ── Protocol messages ─────────────────────────────────────────────

  void send_init() {
    const auto& spec = *m_ctrl->m_device.m_spec;
    auto body = fmt::format(
        R"({{"run":{},"channel":{},"startedAt":{},)"
        R"("header":{{"version":[1,2],)"
        R"("levelCount":{},"commandCount":{},"timingCount":{},)"
        R"("channelWidth":{},"prefetchSize":{},"dq":{},)"
        R"("channelId":{},"readLatency":{},"dramType":{}}},)"
        R"("spec":{{"levelNames":{},"levelSizes":{},)"
        R"("commandNames":{},"commandMeta":{},"commandCycles":{},)"
        R"("timingNames":{},"timingValues":{}}}}})",
        json_str(m_run), m_ctrl->m_channel_id, m_started_at,
        spec.level_count, spec.command_count, spec.timing_count,
        spec.channel_width, spec.internal_prefetch_size, spec.organization.dq,
        m_ctrl->m_channel_id, spec.read_latency, json_str(m_dram_type),
        json_str_array(spec.level_names), json_int_array(spec.organization.level_sizes),
        json_str_array(spec.command_names), json_cmd_meta(spec.command_meta),
        json_int_array(spec.command_cycles),
        json_str_array(spec.timing_names), json_int_array(spec.timing_vals));
    post("init", body);
  }

  /// A batch as columns: clk and req as differences from the previous event (the first is
  /// absolute), wait = clk - arrive (-1 without a request), then cmd, type, src and one addr
  /// column per level.
  std::string encode(const Batch& b) const {
    const size_t n = b.clk.size();
    std::string cols;
    cols.reserve(n * (20 + 4 * m_level_count) + 64);
    fmt::format_to(std::back_inserter(cols), R"({{"n":{},)", n);
    append_column(cols, "clk", n, [&](size_t i) { return b.clk[i] - (i ? b.clk[i - 1] : 0); });
    append_column(cols, "wait", n, [&](size_t i) { return b.arrive[i] < 0 ? -1 : b.clk[i] - b.arrive[i]; });
    append_column(cols, "req", n, [&](size_t i) { return b.id[i] - (i ? b.id[i - 1] : 0); });
    append_column(cols, "cmd", n, [&](size_t i) { return static_cast<int>(b.cmd[i]); });
    append_column(cols, "type", n, [&](size_t i) { return static_cast<int>(b.type[i]); });
    append_column(cols, "src", n, [&](size_t i) { return b.source[i]; });
    cols += R"("addr":[)";
    for (int k = 0; k < m_level_count; k++) {
      if (k) cols += ',';
      cols += '[';
      for (size_t i = 0; i < n; i++) {
        if (i) cols += ',';
        fmt::format_to(std::back_inserter(cols), "{}", b.addr[k][i]);
      }
      cols += ']';
    }
    cols += "]}";
    return cols;
  }

  /// Acts on the server's answer to events or status: a viewer stopped the stream, or the server
  /// restarted and no longer knows this session.
  void handle_reply(const std::string& reply) {
    if (reply_flag(reply, "interrupted")) {
      fmt::print("[LiveTraceStreamer] A viewer stopped the stream. The simulation continues.\n");
      m_stopped = true;
    } else if (reply_flag(reply, "unknown")) {
      send_init();
    }
  }

  /// Whether a JSON reply holds "key": true (with any whitespace around the colon).
  static bool reply_flag(const std::string& reply, const std::string& key) {
    const std::string quoted = "\"" + key + "\"";
    size_t at = reply.find(quoted);
    if (at == std::string::npos) return false;
    at = reply.find_first_not_of(" \t\r\n", at + quoted.size());
    if (at == std::string::npos || reply[at] != ':') return false;
    at = reply.find_first_not_of(" \t\r\n", at + 1);
    return at != std::string::npos && reply.compare(at, 4, "true") == 0;
  }

  // ── Minimal HTTP client ───────────────────────────────────────────

  /// Posts to the visualizer and returns the response body.  On failure, prints why and stops
  /// streaming.
  std::string post(const std::string& type, const std::string& body, const std::string& extra_query = "") {
    if (m_stopped) return "";
    int status = 0;
    std::string response;
    if (!try_http_post(fmt::format("type={}&session={}{}", type, m_session, extra_query), body, status, response)) {
      fmt::print("[LiveTraceStreamer] Visualizer at {}:{} is not reachable or not responding. Stopping stream.\n",
                 m_host, m_port);
      m_stopped = true;
    } else if (status == 401) {
      if (m_token.empty()) {
        fmt::print(
            "[LiveTraceStreamer] Visualizer at {}:{} needs its access token: set RAMULATOR_VISUALIZER_TOKEN. "
            "Stopping stream.\n",
            m_host, m_port);
      } else {
        fmt::print(
            "[LiveTraceStreamer] Visualizer at {}:{} does not accept the access token it was given; it may have "
            "started since with a new one. Stopping stream.\n",
            m_host, m_port);
      }
      m_stopped = true;
    } else if (status < 200 || status >= 300) {
      fmt::print("[LiveTraceStreamer] Visualizer at {}:{} refused the stream ({}): {} Stopping stream.\n", m_host,
                 m_port, status, response);
      m_stopped = true;
    }
    return response;
  }

  /// Returns false if the server could not be reached or did not answer in time.
  bool try_http_post(const std::string& query, const std::string& body, int& status, std::string& response) {
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* addrs = nullptr;
    if (::getaddrinfo(m_host.c_str(), std::to_string(m_port).c_str(), &hints, &addrs) != 0) return false;

    int fd = -1;
    for (auto* a = addrs; a; a = a->ai_next) {
      fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (fd < 0) continue;
      // Bounds connect(), send() and recv(), so a stalled server cannot hang the simulation.
      struct timeval tv {};
      tv.tv_sec = kSocketTimeoutS;
      ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
      if (::connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
      ::close(fd);
      fd = -1;
    }
    ::freeaddrinfo(addrs);
    if (fd < 0) return false;

    const bool ipv6_literal = m_host.find(':') != std::string::npos;
    auto request = fmt::format(
        "POST /api/live-trace-push?{} HTTP/1.1\r\n"
        "Host: {}{}{}:{}\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: {}\r\n"
        "{}"
        "Connection: close\r\n"
        "\r\n",
        query, ipv6_literal ? "[" : "", m_host, ipv6_literal ? "]" : "", m_port, body.size(),
        m_token.empty() ? "" : fmt::format("Authorization: Bearer {}\r\n", m_token));
    request += body;

    size_t sent = 0;
    while (sent < request.size()) {
      ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
      if (n <= 0) {
        ::close(fd);
        return false;
      }
      sent += static_cast<size_t>(n);
    }

    std::string raw;
    char buf[4096];
    ssize_t bytes_read;
    while ((bytes_read = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
      raw.append(buf, bytes_read);
      if (raw.size() > kMaxReplyBytes) {
        ::close(fd);
        return false;
      }
    }
    ::close(fd);
    if (bytes_read != 0) return false;

    // "HTTP/1.1 200 OK\r\n<headers>\r\n\r\n<body>"
    auto space = raw.find(' ');
    if (space == std::string::npos) return false;
    status = std::atoi(raw.c_str() + space + 1);
    auto head_end = raw.find("\r\n\r\n");
    response = head_end == std::string::npos ? "" : raw.substr(head_end + 4);
    return true;
  }
};

}  // namespace Ramulator
