#include <fmt/format.h>
#include <optional>
#include <random>
#include <string>

#include "ramulator/base/param.h"
#include "ramulator/frontend/i_frontend.h"

namespace Ramulator {

class LatencyThroughputTrace : public IFrontEnd, public Implementation {
  // Latency-throughput evaluation frontend with streaming load and pointer-chasing probe.
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, LatencyThroughputTrace, "LatencyThroughputTrace")

 private:
  enum class LatencyMeasureMode { RandomProbe, StreamOnly };

  // Address generation recipe (pre-computed by Python, zero DRAM semantics)
  int m_addr_vec_size;
  std::vector<int> m_bank_positions;  // which addr_vec slots to decompose flat bank into
  std::vector<int> m_bank_counts;     // count per bank slot
  int m_total_bank_units;
  int m_row_pos;  // addr_vec slot index
  int m_col_pos;  // addr_vec slot index
  int m_num_rows;
  int m_num_cols;
  int m_internal_prefetch_size;
  int m_num_cls;
  int m_stream_cls;  // request-sized column slots accessed per row
  bool m_stagger_stream_rows = false;

  // Streaming state (MESS-style alternation with NOP-based rate control)
  int m_nop_counter;  // NOP: interval between consequtive streaming requests to adjust load
  int m_curr_nop = 0;
  size_t m_streaming_idx = 0;  // Flat index: bank(fast) -> col -> row(slow)
  int m_read_ratio = 100;      // Percentage of streaming requests that are reads (0-100)
  bool m_issue_probe = false;  // Alternating flag: false=stream turn, true=probe turn

  // Streaming-only mode: disables probes entirely, fires streaming requests
  // as fast as the memory system can accept them.
  bool m_streaming_only = false;
  int m_num_streaming_requests = 0;

  // Pointer-chasing state
  int m_num_probe_requests = 0;
  std::string m_latency_measure_mode_name = "random-probe";
  LatencyMeasureMode m_latency_measure_mode = LatencyMeasureMode::RandomProbe;
  int m_latency_sample_count = 0;
  int m_warmup_cycles;
  bool m_warmup_enabled = false;
  bool m_warmup_reset_done = false;
  bool m_probe_inflight = false;
  int m_stream_latency_samples_inflight = 0;
  bool m_retry_stream_req_tracks_latency = false;

  // Retry state (MESS-style: retry same request on backpressure)
  std::optional<Request> m_retry_stream_req;
  std::optional<Request> m_retry_probe_req;

  // PRNG for probe addresses and read/write selection
  std::mt19937_64 m_rng;
  std::uniform_int_distribution<int> m_ratio_dist{0, 99};
  uint64_t m_seed = 12345ULL;

  // Stats
  size_t s_streaming_sent = 0;
  int s_latency_samples_completed = 0;
  int s_probes_completed = 0;
  int64_t s_total_probe_latency = 0;
  float s_avg_probe_latency = 0.0f;
  int s_stream_latency_samples_completed = 0;
  int64_t s_total_stream_latency = 0;
  float s_avg_stream_latency = 0.0f;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_clock_ratio, unsigned int, "clock_ratio").required();
    RAMULATOR_PARSE_PARAM(m_nop_counter, int, "nop_counter").required();
    RAMULATOR_PARSE_PARAM(m_num_probe_requests, int, "num_probe_requests").default_val(0);
    RAMULATOR_PARSE_PARAM(m_latency_measure_mode_name, std::string, "latency_measure_mode")
        .default_val("random-probe");
    RAMULATOR_PARSE_PARAM(m_latency_sample_count, int, "latency_sample_count").default_val(0);
    RAMULATOR_PARSE_PARAM(m_streaming_only, bool, "streaming_only").default_val(false);
    RAMULATOR_PARSE_PARAM(m_num_streaming_requests, int, "num_streaming_requests").default_val(0);
    RAMULATOR_PARSE_PARAM(m_stream_cls, int, "stream_cls").default_val(8);
    RAMULATOR_PARSE_PARAM(m_stagger_stream_rows, bool, "stagger_stream_rows").default_val(false);
    RAMULATOR_PARSE_PARAM(m_warmup_cycles, int, "warmup_cycles").default_val(10000);
    RAMULATOR_PARSE_PARAM(m_read_ratio, int, "read_ratio").default_val(100);
    RAMULATOR_PARSE_PARAM(m_seed, uint64_t, "seed").default_val(12345ULL);

    // Address generation recipe (injected by Python from DRAM object)
    RAMULATOR_PARSE_PARAM(m_addr_vec_size, int, "addr_vec_size").required();
    RAMULATOR_PARSE_PARAM(m_total_bank_units, int, "total_bank_units").required();
    RAMULATOR_PARSE_PARAM(m_row_pos, int, "row_pos").required();
    RAMULATOR_PARSE_PARAM(m_col_pos, int, "col_pos").required();
    RAMULATOR_PARSE_PARAM(m_num_rows, int, "num_rows").required();
    RAMULATOR_PARSE_PARAM(m_num_cols, int, "num_cols").required();
    RAMULATOR_PARSE_PARAM(m_internal_prefetch_size, int, "internal_prefetch_size").required();
    RAMULATOR_PARSE_PARAM(m_num_cls, int, "num_cls").required();

    RAMULATOR_PARSE_PARAM(m_bank_positions, std::vector<int>, "bank_positions").required();
    RAMULATOR_PARSE_PARAM(m_bank_counts, std::vector<int>, "bank_counts").required();

    m_rng.seed(m_seed);
    m_warmup_enabled = (m_warmup_cycles > 0);
    configure_latency_measurement();

    // Validate streaming_only + num_streaming_requests coupling
    if (m_streaming_only && m_num_streaming_requests <= 0) {
      throw std::runtime_error(
          "LatencyThroughputTrace: num_streaming_requests must be set when streaming_only=true");
    }

    m_stats.add("streaming_requests_sent", s_streaming_sent);
    m_stats.add("probe_requests_completed", s_probes_completed);
    m_stats.add("total_probe_latency", s_total_probe_latency);
    m_stats.add("avg_probe_latency", s_avg_probe_latency);
    m_stats.add("stream_latency_samples_completed", s_stream_latency_samples_completed);
    m_stats.add("total_stream_latency", s_total_stream_latency);
    m_stats.add("avg_stream_latency", s_avg_stream_latency);

    m_logger.info(
        fmt::format("LatencyThroughputTrace: nop_counter={}, latency_mode={}, samples={}, warmup={}, bank_units={}",
                    m_nop_counter, m_latency_measure_mode_name, m_latency_sample_count, m_warmup_cycles,
                    m_total_bank_units));
  }

  int get_num_cores() override {
    return 2;
  }  // source_id 0=streaming, 1=probe

  void tick() override {
    m_clk++;

    maybe_reset_warmup_stats();

    if (m_streaming_only) {
      // Streaming-only mode: no NOP rate-limiting, no probes.
      // Fire streaming requests as fast as the memory system can accept them.
      tick_stream_only();
      return;
    }

    // Normal latency-throughput sweep mode.
    if (tick_nop()) {
      return;
    }
    if (m_latency_measure_mode == LatencyMeasureMode::StreamOnly) {
      tick_stream();
      return;
    }
    if (m_issue_probe && m_probe_inflight) {
      m_issue_probe = false;
      return;
    }
    if (m_issue_probe) {
      tick_probe();
    } else {
      tick_stream();
    }
  }

  bool is_finished() override {
    if (m_warmup_enabled && !m_warmup_reset_done) {
      return false;
    }
    if (m_streaming_only) {
      return static_cast<int>(s_streaming_sent) >= m_num_streaming_requests;
    }
    return s_latency_samples_completed >= m_latency_sample_count;
  }

  void finalize() override {
    if (s_probes_completed > 0) {
      s_avg_probe_latency = static_cast<float>(s_total_probe_latency) / s_probes_completed;
    }
    if (s_stream_latency_samples_completed > 0) {
      s_avg_stream_latency =
          static_cast<float>(s_total_stream_latency) / s_stream_latency_samples_completed;
    }
  }

  void reset_stats() override {
    s_streaming_sent = 0;
    s_latency_samples_completed = 0;
    s_probes_completed = 0;
    s_total_probe_latency = 0;
    s_avg_probe_latency = 0.0f;
    s_stream_latency_samples_completed = 0;
    s_total_stream_latency = 0;
    s_avg_stream_latency = 0.0f;
  }

 private:
  void configure_latency_measurement() {
    if (m_latency_measure_mode_name == "random-probe") {
      m_latency_measure_mode = LatencyMeasureMode::RandomProbe;
    } else if (m_latency_measure_mode_name == "stream-only") {
      m_latency_measure_mode = LatencyMeasureMode::StreamOnly;
    } else {
      throw std::runtime_error(fmt::format(
          "LatencyThroughputTrace: invalid latency_measure_mode '{}'; expected 'random-probe' or 'stream-only'",
          m_latency_measure_mode_name));
    }

    if (m_streaming_only) {
      if (m_latency_measure_mode == LatencyMeasureMode::StreamOnly) {
        throw std::runtime_error(
            "LatencyThroughputTrace: latency_measure_mode='stream-only' cannot be combined with "
            "streaming_only=true");
      }
      return;
    }

    if (m_latency_measure_mode == LatencyMeasureMode::RandomProbe && m_latency_sample_count <= 0 &&
        m_num_probe_requests > 0) {
      m_latency_sample_count = m_num_probe_requests;
    }
    if (m_latency_sample_count <= 0) {
      throw std::runtime_error(
          "LatencyThroughputTrace: latency_sample_count must be positive for latency-throughput sweeps");
    }
    if (m_latency_measure_mode == LatencyMeasureMode::StreamOnly && m_read_ratio <= 0) {
      throw std::runtime_error(
          "LatencyThroughputTrace: latency_measure_mode='stream-only' requires read_ratio > 0");
    }
    if (m_latency_measure_mode == LatencyMeasureMode::RandomProbe) {
      m_num_probe_requests = m_latency_sample_count;
    }
  }

  void maybe_reset_warmup_stats() {
    if (!m_warmup_enabled || m_warmup_reset_done || m_clk <= m_warmup_cycles) {
      return;
    }
    m_warmup_reset_done = true;
    reset_stats_recursive();
    m_memory_system->reset_stats_recursive();
  }

  // Returns true if this tick should be skipped (NOP rate-limiting).
  // NOP skips only apply to stream turns: skip N-1 out of every N stream turns.
  bool tick_nop() {
    if (m_issue_probe) {
      return false;
    }
    bool is_nop = (m_nop_counter > 1 && m_curr_nop != 0);
    m_curr_nop = (m_curr_nop + 1) % m_nop_counter;
    if (is_nop && m_latency_measure_mode == LatencyMeasureMode::RandomProbe) {
      m_issue_probe = !m_issue_probe;
    }
    return is_nop;
  }

  // Handle probe turn: issue a random-address read to measure latency under load.
  // On backpressure, the request is held in m_retry_probe_req for the next attempt.
  void tick_probe() {
    bool want_probe =
        ((!m_warmup_enabled || m_warmup_reset_done) && s_probes_completed < m_latency_sample_count);
    if (!want_probe) {
      m_issue_probe = false;
      return;
    }

    if (!m_retry_probe_req) {
      Request req = make_request(random_addr_vec(), Request::Type::Read, 1);
      req.callback = [this](Request& completed) {
        record_probe_latency(completed);
        m_probe_inflight = false;
      };
      m_retry_probe_req = req;
    }
    if (m_memory_system->send(*m_retry_probe_req)) {
      m_probe_inflight = true;
      m_issue_probe = false;
      m_retry_probe_req.reset();
    }
  }

  // Streaming-only mode: issue sequential reads/writes at maximum rate.
  // No NOP rate-limiting, no probe alternation.
  void tick_stream_only() {
    if (!m_retry_stream_req) {
      int type = Request::Type::Read;
      if (m_read_ratio < 100) {
        type = (m_ratio_dist(m_rng) < m_read_ratio) ? Request::Type::Read : Request::Type::Write;
      }
      m_retry_stream_req = make_request(streaming_addr_vec(m_streaming_idx), type, 0);
    }
    if (m_memory_system->send(*m_retry_stream_req)) {
      s_streaming_sent++;
      m_streaming_idx++;
      m_retry_stream_req.reset();
    }
  }

  // Handle stream turn: issue sequential accesses with configurable read/write mix.
  // On backpressure, the request is held in m_retry_stream_req for the next attempt.
  void tick_stream() {
    if (!m_retry_stream_req) {
      int type = Request::Type::Read;
      if (m_read_ratio < 100) {
        type = (m_ratio_dist(m_rng) < m_read_ratio) ? Request::Type::Read : Request::Type::Write;
      }
      Request req = make_request(streaming_addr_vec(m_streaming_idx), type, 0);
      m_retry_stream_req_tracks_latency = false;
      if (type == Request::Type::Read && should_sample_stream_latency()) {
        req.callback = [this](Request& completed) {
          m_stream_latency_samples_inflight--;
          record_stream_latency(completed);
        };
        m_retry_stream_req_tracks_latency = true;
      }
      m_retry_stream_req = req;
    }
    if (m_memory_system->send(*m_retry_stream_req)) {
      if (m_retry_stream_req_tracks_latency) {
        m_stream_latency_samples_inflight++;
      }
      s_streaming_sent++;
      m_streaming_idx++;
      if (m_latency_measure_mode == LatencyMeasureMode::RandomProbe) {
        m_issue_probe = true;
      }
      m_retry_stream_req_tracks_latency = false;
      m_retry_stream_req.reset();
    }
  }

  bool should_sample_stream_latency() const {
    return m_latency_measure_mode == LatencyMeasureMode::StreamOnly &&
           (!m_warmup_enabled || m_warmup_reset_done) &&
           (s_stream_latency_samples_completed + m_stream_latency_samples_inflight < m_latency_sample_count);
  }

  void record_probe_latency(Request& completed) {
    Clk_t latency = completed.depart - completed.arrive;
    s_total_probe_latency += latency;
    s_probes_completed++;
    s_latency_samples_completed++;
  }

  void record_stream_latency(Request& completed) {
    Clk_t latency = completed.depart - completed.arrive;
    s_total_stream_latency += latency;
    s_stream_latency_samples_completed++;
    s_latency_samples_completed++;
  }

  // Build a Request from an address vector, setting the flat address for write-forwarding.
  Request make_request(const AddrVec_t& av, int type, int source_id) {
    Request req(av, type);
    int bank_flat = 0;
    for (size_t i = 0; i < m_bank_positions.size(); i++) {
      bank_flat = bank_flat * m_bank_counts[i] + av[m_bank_positions[i]];
    }
    int cls = av[m_col_pos] / m_internal_prefetch_size;
    req.addr = static_cast<Addr_t>(bank_flat * m_num_rows * m_num_cls + av[m_row_pos] * m_num_cls + cls);
    req.source_id = source_id;
    req.size_bytes = m_memory_system->get_tx_bytes();
    return req;
  }

  // Streaming pattern: bank(fastest) -> cacheline slot -> row(slowest).
  // Uses m_stream_cls (not full m_num_cls) to limit row-hit sequence length.
  AddrVec_t streaming_addr_vec(size_t idx) {
    AddrVec_t av(m_addr_vec_size, 0);
    int flat_bank = static_cast<int>(idx % m_total_bank_units);
    size_t per_bank_idx = idx / m_total_bank_units;
    if (m_stagger_stream_rows && m_stream_cls > 0) {
      size_t offset = 0;
      if (m_total_bank_units <= m_stream_cls) {
        offset = static_cast<size_t>(flat_bank) * m_stream_cls / m_total_bank_units;
      } else {
        size_t banks_per_phase =
            (static_cast<size_t>(m_total_bank_units) + m_stream_cls - 1) / m_stream_cls;
        offset = (static_cast<size_t>(flat_bank) / banks_per_phase) % m_stream_cls;
      }
      per_bank_idx += offset;
    }
    int cls = static_cast<int>(per_bank_idx % m_stream_cls);
    int col = cls * m_internal_prefetch_size;
    int row = static_cast<int>((per_bank_idx / m_stream_cls) % m_num_rows);
    decompose_bank(flat_bank, av);
    av[m_row_pos] = row;
    av[m_col_pos] = col;
    return av;
  }

  // Random address for pointer-chasing probes (targets random rows for row-buffer misses).
  AddrVec_t random_addr_vec() {
    AddrVec_t av(m_addr_vec_size, 0);
    for (size_t i = 0; i < m_bank_positions.size(); i++) {
      std::uniform_int_distribution<int> dist(0, m_bank_counts[i] - 1);
      av[m_bank_positions[i]] = dist(m_rng);
    }
    std::uniform_int_distribution<int> row_dist(0, m_num_rows - 1);
    std::uniform_int_distribution<int> cls_dist(0, m_num_cls - 1);
    av[m_row_pos] = row_dist(m_rng);
    av[m_col_pos] = cls_dist(m_rng) * m_internal_prefetch_size;
    return av;
  }

  // Mixed-radix decomposition: map flat bank index into per-slot addr_vec values.
  // Last entry in m_bank_positions cycles fastest.
  void decompose_bank(int flat, AddrVec_t& av) {
    for (int i = static_cast<int>(m_bank_positions.size()) - 1; i >= 0; i--) {
      av[m_bank_positions[i]] = flat % m_bank_counts[i];
      flat /= m_bank_counts[i];
    }
  }
};

}  // namespace Ramulator
