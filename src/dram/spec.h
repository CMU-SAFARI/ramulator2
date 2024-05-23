#ifndef RAMULATOR_DEVICE_SPEC_H
#define RAMULATOR_DEVICE_SPEC_H

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <array>
#include <ranges>
#include <stdexcept>

#include <spdlog/spdlog.h>

namespace Ramulator {

using Level_t = int;
using Command_t = int;
using State_t = int;

// Organization hierarchy of the device
struct Organization {
  /// The density of the chip in Mb.
  int density = -1;
  /// The DQ width.
  int dq = -1;
  /// The size of different levels in the hierarchy.
  std::vector<int> count;
}; 

// Meta information about a command
struct DRAMCommandMeta {
  bool is_opening = false;
  bool is_closing = false;
  bool is_accessing = false;
  bool is_refreshing = false;
  bool is_sb_cmd = false;
};

// Future action entries
struct FutureAction {
  Command_t cmd;
  AddrVec_t addr_vec;
  Clk_t clk;
};

// Timing Constraint
struct TimingConsEntry {
  /// The command that the timing constraint is constraining.
  int cmd;
  /// The value of the timing constraint (in number of cycles).
  int val;
  /// How long of a history to keep track of?
  int window = 1;
  /// Whether this timing constraint is affecting siblings in the same level.
  bool sibling = false;

  TimingConsEntry(int cmd, int val, int window = 1, bool sibling = false):
  cmd(cmd), val(val), window(window), sibling(sibling) {
    if (this->window < 0) {
      spdlog::warn("[DRAM Spec] Timing constraint value smaller than 0!");
      this->window = 0;
    }
  };
};

using TimingCons = std::vector<std::vector<std::vector<TimingConsEntry>>>;

// // TODO: Write a expression parser and evaluator
// template<class T>
// int EvalTimingExpr(T* spec, std::string_view expr) {
//   static const std::unordered_map<std::string, int> precedence = {
//     {'+', 1}, {'-', 1}, {'*', 2}, {'/', 2}, {'^', 3}
//   };
// }

struct TimingConsInitializer {
  std::string_view level;
  std::vector<std::string_view> preceding;
  std::vector<std::string_view> following;
  int latency = -1;
  int window = 1;
  bool is_sibling = false;
};

template<class T>
void populate_timingcons(T* spec, std::vector<TimingConsInitializer> initializer) {
  spec->m_timing_cons.resize(T::m_levels.size(), std::vector<std::vector<TimingConsEntry>>(T::m_commands.size()));
  for (const auto& ts : initializer) {
    int level = T::m_levels(ts.level);  // cannot be consteval...
    for (auto p_cmd_str : ts.preceding) {
      int p_cmd = T::m_commands(p_cmd_str);
      for (auto f_cmd_str : ts.following) {
        int f_cmd = T::m_commands(f_cmd_str);
        spec->m_timing_cons[level][p_cmd].push_back({f_cmd, ts.latency, ts.window, ts.is_sibling});
      }
    }
  }
};


template<int N>
class ImplDef;

/**
 * @brief    Definition data structure used in the DRAM interface.
 * @details
 * Definition data structure used in the DRAM interface. 
 * Accessible to others at *runtime* (no consteval!).
 * 
 */
class SpecDef : public std::vector<std::string_view> {
  private:
    std::unordered_map<std::string_view, int> m_str2int_map;

  public:
    SpecDef() = default;
    template <int N> constexpr SpecDef(const ImplDef<N>& spec):
    std::vector<std::string_view>(spec.begin(), spec.end()) {
      for (int i = 0; i < spec.size(); i++) {
        m_str2int_map[spec.std::template array<std::string_view, N>::operator[](i)] = i;
      }
    }

    bool contains(std::string_view name) const {
      return m_str2int_map.contains(name);
    }

    std::string_view operator()(int i) const {
      return operator[](i);
    };
    int operator()(std::string_view name) const {
      return operator[](name);
    };

  private:
    std::string_view operator[](int i) const {
      if (i < size()) {
        return std::vector<std::string_view>::operator[](i);
      } else {
        throw std::out_of_range("");
      }
    };
    int operator[](std::string_view name) const {
      return m_str2int_map.at(name);
    };
};


template<int N>
class ImplDef : public std::array<std::string_view, N> {
  public:
    consteval std::string_view operator[](int i) const {
      if (i < N) {
        return std::array<std::string_view, N>::operator[](i);
      } else {
        throw "NON EXISTENT ID";
      }
    };
    consteval int operator[](std::string_view name) const {
      for (int i = 0; i < N; i++) {
        if (std::array<std::string_view, N>::operator[](i) == name) {
          return i;
        } 
      }
      throw "NON EXISTENT NAME";
    };

    constexpr std::string_view operator()(int i) const {
      if (i < N) {
        return std::array<std::string_view, N>::operator[](i);
      } else {
        throw "NON EXISTENT ID";
      }
    };
    constexpr int operator()(std::string_view name) const {
      for (int i = 0; i < N; i++) {
        if (std::array<std::string_view, N>::operator[](i) == name) {
          return i;
        } 
      }
      throw "NON EXISTENT NAME";
    };
};

template <typename... Ts>
ImplDef(Ts&&... elems) -> ImplDef<sizeof...(Ts)>;


template<int N, int M, typename V>
class ImplLUT;

template<typename V = int>
class SpecLUT : public std::vector<V> {
  private:
    const SpecDef* m_key_def = nullptr;
  public:
    SpecLUT(const SpecDef& key_def) : m_key_def(&key_def) {};

    template <int N, int M>
    SpecLUT& operator=(const ImplLUT<N, M, V>& spec) {
      this->assign(spec.begin(), spec.end());
      return *this;
    }

    SpecLUT& operator=(const std::vector<V>& preset) {
      this->assign(preset.begin(), preset.end());
      return *this;
    }

    V& operator()(std::string_view name) {
      return operator[](name);
    }
    V& operator()(int id) {
      return operator[](id);
    }

  private:
    V& operator[](std::string_view name) {
      int id = (*m_key_def)(name);
      return std::vector<V>::operator[](id);
    }

    V& operator[](int id) {
      if (id < m_key_def->size()) {
        return std::vector<V>::operator[](id);
      } else {
        throw std::out_of_range("SpecLUT out of range");
      }
    }    
};

template<int N, int M, typename V>
class ImplLUT : public std::array<V, N> {
  private:
    const ImplDef<N>* m_key_def = nullptr;

  public:
    using std::array<V, N>::operator[];

    ImplLUT(const ImplDef<N>& key_def, const std::map<std::string_view, V>& lut) {
      m_key_def = &key_def;
      for (const auto [key_str, value] : lut) {
        int key_id = key_def(key_str);
        std::array<V, N>::operator[](key_id) = value;
      }
    }

    const V& operator[](std::string str) const {
      int key_id = (*m_key_def)[str];
      return std::vector<V>::operator[](key_id);
    }
};

template<int N, int M>
class ImplLUT<N, M, int> : public std::array<int, N> {
  private:
    const ImplDef<N>* m_key_def = nullptr;

  public:
    using std::array<int, N>::operator[];

    ImplLUT(
        const ImplDef<N>& key_def, 
        const ImplDef<M>& value_def, 
        const std::map<std::string_view, std::string_view>& lut
    ) {
      m_key_def = &key_def;
      for (const auto [key_str, value_str] : lut) {
        int key_id = key_def(key_str);
        int value_id = value_def(value_str);
        std::array<int, N>::operator[](key_id) = value_id;
      }
    }
    const int& operator[](std::string str) const {
      int key_id = (*m_key_def)[str];
      return std::vector<int>::operator[](key_id);
    }
};

template <int N, typename V>
ImplLUT(ImplDef<N>, std::map<std::string_view, V>) -> ImplLUT<N, 0, V>;

template <int N, int M>
ImplLUT(ImplDef<N>, ImplDef<M>, std::map<std::string_view, std::string_view>) -> ImplLUT<N, M, int>;

template<typename V, int N>
ImplLUT<N, 0, V> LUT (const ImplDef<N>& key_def, const std::map<std::string_view, V>& lut) {
  return ImplLUT<N, 0, V>(key_def, lut);
};

template<int N, int M>
ImplLUT<N, M, int> LUT (const ImplDef<N>& key_def, const ImplDef<M>& value_def, const std::map<std::string_view, std::string_view>& lut) {
  return ImplLUT<N, M, int>(key_def, value_def, lut);
};

struct PowerStats {
  public:
    int rank_id = -1;

    enum class PowerState {
      IDLE = 0,
      ACTIVE = 1,
      REFRESHING = 2
    };
    PowerState cur_power_state = PowerState::IDLE;

    double act_background_energy = 0;
    double pre_background_energy = 0;

    double total_background_energy = 0;
    double total_cmd_energy = 0;
    double total_energy = 0;

    std::vector<size_t> cmd_counters;

    Clk_t active_cycles = 0;
    Clk_t idle_cycles = 0;

    Clk_t active_start_cycle = -1; // initially rank is not active
    Clk_t idle_start_cycle = 0;
    
};        

}// namespace Ramulator

#endif   // RAMULATOR_DEVICE_DEVICE_H
