#ifndef RAMULATOR_PLUGIN_BLOCKHAMMER_FILTER_
#define RAMULATOR_PLUGIN_BLOCKHAMMER_FILTER_

#include <functional>
#include <cstdint>

namespace Ramulator {

typedef std::function<uint32_t(uint32_t)> bloom_hash_fn;

template <typename elem_t>
struct HistoryEntry {
  elem_t entry;
  uint64_t timestamp;
};

template <typename elem_t>
class IBloomFilter {
public:
  virtual void insert(elem_t elem) = 0;
  virtual bool test(elem_t elem) = 0;
  virtual void reset() = 0;
};      // class IBloomFilter

template <typename elem_t, typename ctr_t>
class CountingBloomFilter : public IBloomFilter<elem_t> {
public:
  CountingBloomFilter(int num_counters, int ctr_thresh, bool saturate,
        std::vector<bloom_hash_fn>& hash_functions) : m_hash_functions(hash_functions) {
      // Initializers looks ugly here, opting for manual assignment
      this->m_num_counters = num_counters;
      this->m_ctr_thresh = ctr_thresh;
      this->m_saturate = m_saturate;
      m_counters.resize(m_num_counters);
      reset();
  }

  ~CountingBloomFilter() {
    m_counters.clear();
  }

  virtual void insert(elem_t elem) override {
    for (int i = 0; i < m_hash_functions.size(); i++) {
      uint32_t idx = m_hash_functions[i](elem) % m_num_counters;
      if (!m_saturate || m_counters[idx] < m_ctr_thresh) {
        m_counters[idx]++;
      }
    }
  }

  virtual bool test(elem_t elem) override {
    bool pass = true;
    for (int i = 0; i < m_hash_functions.size(); i++) {
      uint32_t idx = m_hash_functions[i](elem) % m_num_counters;
      pass &= m_counters[idx] >= m_ctr_thresh;
    }
    return pass;
  }

  virtual void reset() override {
    std::fill(m_counters.begin(), m_counters.end(), (ctr_t) 0);
  }

private:
  int m_num_counters;
  int m_ctr_thresh;
  bool m_saturate;
  std::vector<ctr_t> m_counters;
  std::vector<bloom_hash_fn>& m_hash_functions;
};      // class CountingBloomFilter

template <typename elem_t, class T>
class UnifiedBloomFilter : public IBloomFilter<elem_t> {
public:
  UnifiedBloomFilter(std::vector<T*>& filters, int len_epoch, BHO3LLC* llc) : m_filters(filters) {
    static_assert(std::is_base_of<IBloomFilter<elem_t>, T>::value, "Template T must be a subclass of IBloomFilter");
    this->m_len_epoch = len_epoch;
    this->m_tick = 0;
    this->m_test_idx = 0;
    this->m_llc = llc;
  }

  ~UnifiedBloomFilter() {
    m_filters.clear();
  }

  void update() {
    m_tick++;
    if (m_tick >= m_len_epoch) {
      m_tick = 0;
      m_filters[m_test_idx]->reset();
      m_test_idx = (m_test_idx + 1) % m_filters.size();
    }
  }

  virtual void insert(elem_t elem) override {
    for (T* filter : m_filters) {
      filter->insert(elem);
    }
  }

  virtual bool test(elem_t elem) override {
    return m_filters[m_test_idx]->test(elem);
  }

  virtual void reset() override {
    for (T* filter : m_filters) {
      filter->reset();
    }
  }

private:
  int m_len_epoch;
  std::vector<T*>& m_filters;
  uint64_t m_tick;
  uint32_t m_test_idx;
  BHO3LLC* m_llc;
};      // class UnifiedBloomFilter

template <typename elem_t>
class HistoryBuffer {
public:
  // Slight modification, we allow 'max_freq' activations within 'size' ticks
  HistoryBuffer(uint32_t size, uint32_t max_freq) {
    this->m_size = size;
    this->m_max_freq = max_freq;
    this->m_tick = 0;
    history = std::vector<HistoryEntry<elem_t>>(size, {-1, (uint64_t) -1});
  }

  ~HistoryBuffer() {
    history.clear();
    elem_counter.clear();
  }

  inline bool exists(elem_t elem) {
    return elem_counter.find(elem) != elem_counter.end();
  }

  inline bool exceeds(elem_t elem) {
    return elem_counter[elem] >= m_max_freq;
  }

  bool search(elem_t elem) {
    return exists(elem) && exceeds(elem);
  }

  void insert(elem_t elem) {
    history[m_tick % m_size] = {elem, m_tick};
    if (!exists(elem)) {
      elem_counter[elem] = 0;
    }
    elem_counter[elem]++;
  }

  void update() {
    m_tick++;
    elem_t elem = history[m_tick % m_size].entry;
    if (!exists(elem)) {
      return;
    }
    if (--elem_counter[elem] == 0) {
      elem_counter.erase(elem);
    }
  }

private:
  uint64_t m_tick;
  uint32_t m_size;
  uint32_t m_max_freq;
  std::vector<HistoryEntry<elem_t>> history;
  std::unordered_map<elem_t, uint32_t> elem_counter;
};      // class HistoryBuffer
};  // namespace Ramulator

#endif // RAMULATOR_PLUGIN_BLOCKHAMMER_FILTER_