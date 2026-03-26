#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "ramulator/base/config_node.h"
#include "ramulator/base/factory.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/i_controller.h"
#include "ramulator/controller/plugin/controller_validation_hook.h"
#include "ramulator/dram/device.h"
#include "ramulator/dram/dram_spec.h"
#include "ramulator/frontend/i_frontend.h"
#include "ramulator/memory_system/i_memory_system.h"

namespace nb = nanobind;

using namespace Ramulator;

// ---- Python dict/list/scalar -> ConfigNode ----

static ConfigNode py_to_confignode(nb::handle obj) {
  if (nb::isinstance<nb::dict>(obj)) {
    ConfigNode::Map map;
    for (auto [key, val] : nb::cast<nb::dict>(obj)) {
      map[nb::cast<std::string>(key)] = py_to_confignode(val);
    }
    return ConfigNode(std::move(map));
  }
  if (nb::isinstance<nb::list>(obj)) {
    ConfigNode::Seq seq;
    for (auto item : nb::cast<nb::list>(obj)) {
      seq.push_back(py_to_confignode(item));
    }
    return ConfigNode(std::move(seq));
  }
  if (nb::isinstance<nb::bool_>(obj)) {
    return ConfigNode(nb::cast<bool>(obj));
  }
  if (nb::isinstance<nb::int_>(obj)) {
    return ConfigNode(nb::cast<long long>(obj));
  }
  if (nb::isinstance<nb::float_>(obj)) {
    return ConfigNode(nb::cast<double>(obj));
  }
  if (nb::isinstance<nb::str>(obj)) {
    return ConfigNode(nb::cast<std::string>(obj));
  }
  if (obj.is_none()) {
    return ConfigNode{};
  }

  throw std::runtime_error("py_to_confignode: unsupported Python type");
}

// ---- ConfigNode -> Python object ----

static nb::object confignode_to_py(const ConfigNode& node) {
  if (node.is_null()) {
    return nb::none();
  }
  if (node.is_map()) {
    nb::dict d;
    for (const auto& [k, v] : node.map()) {
      d[nb::cast(k)] = confignode_to_py(v);
    }
    return d;
  }
  if (node.is_sequence()) {
    nb::list l;
    for (const auto& item : node.seq()) {
      l.append(confignode_to_py(item));
    }
    return l;
  }

  const auto& s = node.scalar();
  if (s == "true") {
    return nb::cast(true);
  }
  if (s == "false") {
    return nb::cast(false);
  }
  try {
    size_t pos;
    long long i = std::stoll(s, &pos);
    if (pos == s.size()) {
      return nb::cast(i);
    }
  } catch (...) {
  }
  try {
    size_t pos;
    double d = std::stod(s, &pos);
    if (pos == s.size()) {
      return nb::cast(d);
    }
  } catch (...) {
  }
  return nb::cast(s);
}

static ConfigNode wrap_interface_config(const std::string& ifce_name, ConfigNode config) {
  return ConfigNode(ConfigNode::Map{{ifce_name, std::move(config)}});
}

static ConfigNode inject_issued_command_validation_hook(ConfigNode controller_config) {
  ConfigNode plugins = controller_config["controller_plugins"];
  if (plugins.is_null()) {
    plugins = ConfigNode::Seq{};
  }
  plugins.push_back(ConfigNode::Map{{"impl", ConfigNode("IssuedCommandValidationHook")}});
  controller_config.set("controller_plugins", plugins);
  return controller_config;
}

static void validate_addr_vec_size(const DRAMSpec& spec, const AddrVec_t& addr_vec) {
  if (static_cast<int>(addr_vec.size()) != spec.level_count) {
    throw std::runtime_error("addr_vec size mismatch: expected " + std::to_string(spec.level_count) + ", got " +
                             std::to_string(addr_vec.size()));
  }
}

static std::map<std::string, int> timing_map(const DRAMSpec& spec) {
  std::map<std::string, int> out;
  for (int i = 0; i < spec.timing_count; i++) {
    out[spec.timing_names[i]] = spec.timing_vals[i];
  }
  return out;
}

// ---- Simulation wrapper ----

class Simulation {
  std::unique_ptr<IFrontEnd> m_frontend;
  std::unique_ptr<IMemorySystem> m_memory_system;

 public:
  explicit Simulation(nb::dict config) {
    ConfigNode cfg = py_to_confignode(config);

    m_frontend.reset(Factory::create_frontend(cfg));
    m_memory_system.reset(Factory::create_memory_system(cfg));

    m_frontend->connect_memory_system(m_memory_system.get());
    m_memory_system->connect_frontend(m_frontend.get());
  }

  Simulation(const Simulation&) = delete;
  Simulation& operator=(const Simulation&) = delete;

  void run() {
    int fe_tick = m_frontend->get_clock_ratio();
    int mem_tick = m_memory_system->get_clock_ratio();
    if (fe_tick <= 0 || mem_tick <= 0) {
      throw std::runtime_error("clock_ratio must be > 0 for both frontend and memory system");
    }
    int tick_mult = fe_tick * mem_tick;

    for (uint64_t i = 0;; i++) {
      if (((i % tick_mult) % mem_tick) == 0) {
        m_frontend->tick();
      }

      if (m_frontend->is_finished()) {
        break;
      }

      if ((i % tick_mult) % fe_tick == 0) {
        m_memory_system->tick();
      }
    }
  }

  nb::dict get_stats() {
    m_frontend->finalize();
    m_memory_system->finalize();

    ConfigNode::Map root;
    root["frontend"] = m_frontend->collect_stats();
    root["memory_system"] = m_memory_system->collect_stats();

    return nb::cast<nb::dict>(confignode_to_py(ConfigNode(std::move(root))));
  }

  std::string get_stats_yaml() {
    m_frontend->finalize();
    m_memory_system->finalize();

    std::ostringstream ss;
    m_frontend->print_stats(ss);
    m_memory_system->print_stats(ss);
    return ss.str();
  }
};

// ---- DeviceUnderTest direct binding ----

class DeviceUnderTestCpp {
 public:
  explicit DeviceUnderTestCpp(nb::dict dram_config) {
    std::string impl = nb::cast<std::string>(dram_config["impl"]);
    ConfigNode cfg = wrap_interface_config("dram", py_to_confignode(dram_config));
    m_device.init(create_dram_spec(impl, cfg));
  }

  std::vector<std::string> level_names() const {
    return spec().level_names;
  }

  std::vector<std::string> command_names() const {
    return spec().command_names;
  }

  std::map<std::string, int> timings() const {
    return timing_map(spec());
  }

  int timing(const std::string& name) const {
    return spec().get_timing_value(name);
  }

  nb::dict probe(const std::string& command_name, const AddrVec_t& addr_vec, Clk_t clk) {
    validate_addr_vec_size(spec(), addr_vec);
    int cmd = spec().get_command_id(command_name);

    int preq = m_device.get_preq_command(cmd, addr_vec, clk);
    bool timing_ok = m_device.check_timing(cmd, addr_vec, clk);
    bool ready = (preq == cmd) && timing_ok;

    bool row_hit = false;
    bool row_open = false;
    if (can_query_bank_local_state(cmd, addr_vec)) {
      row_hit = m_device.check_rowbuffer_hit(cmd, addr_vec, clk);
      row_open = m_device.check_node_open(cmd, addr_vec, clk);
    }

    nb::dict out;
    out["preq"] = spec().command_names[preq];
    out["timing_OK"] = timing_ok;
    out["ready"] = ready;
    out["row_hit"] = row_hit;
    out["row_open"] = row_open;
    return out;
  }

  void issue(const std::string& command_name, const AddrVec_t& addr_vec, Clk_t clk) {
    validate_addr_vec_size(spec(), addr_vec);
    int cmd = spec().get_command_id(command_name);

    int preq = m_device.get_preq_command(cmd, addr_vec, clk);
    if (preq != cmd) {
      throw std::runtime_error("Cannot issue command '" + command_name + "' at clk=" + std::to_string(clk) +
                               ": prerequisite is '" + spec().command_names[preq] + "'");
    }

    if (!m_device.check_timing(cmd, addr_vec, clk)) {
      throw std::runtime_error("Cannot issue command '" + command_name + "' at clk=" + std::to_string(clk) +
                               ": timing not ready");
    }

    m_device.issue_command(cmd, addr_vec, clk);
  }

 private:
  DRAMDevice m_device;

  const DRAMSpec& spec() const {
    return *m_device.m_spec;
  }

  bool can_query_bank_local_state(int command, const AddrVec_t& addr_vec) const {
    if (!spec().has_level("Bank") || spec().bank_targets[command] != BankTarget::Single) {
      return false;
    }
    int bank_level = spec().get_level_id("Bank");
    for (int lvl = 0; lvl <= bank_level; lvl++) {
      if (addr_vec[lvl] < 0) {
        return false;
      }
    }
    return true;
  }
};

// ---- ControllerUnderTest harness ----

class HarnessFrontEnd final : public IFrontEnd, public Implementation {
 public:
  explicit HarnessFrontEnd(int num_cores)
      : Implementation(ConfigNode(ConfigNode::Map{}), IFrontEnd::get_name(), "HarnessFrontEnd", nullptr),
        m_num_cores(num_cores) {
    IFrontEnd::m_impl = this;
  }

  void init() override {
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
  }

  void tick() override {
  }

  bool is_finished() override {
    return false;
  }

  int get_num_cores() override {
    return m_num_cores;
  }

 protected:
  std::string get_name() const override {
    return "HarnessFrontEnd";
  }

  std::string get_ifce_name() const override {
    return IFrontEnd::get_name();
  }

 private:
  int m_num_cores = 1;
};

class HarnessMemorySystem final : public IMemorySystem, public Implementation {
 public:
  explicit HarnessMemorySystem(ConfigNode controller_config)
      : Implementation(ConfigNode(ConfigNode::Map{}), IMemorySystem::get_name(), "HarnessMemorySystem", nullptr) {
    IMemorySystem::m_impl = this;

    ConfigNode wrapped = wrap_interface_config(IController::get_name(), std::move(controller_config));
    Implementation* controller_impl = Factory::create_implementation(IController::get_name(), wrapped, this);
    add_child(controller_impl);

    m_controller = dynamic_cast<IController*>(controller_impl);
    if (!m_controller) {
      throw std::runtime_error("HarnessMemorySystem failed to create a controller");
    }
    controller_impl->set_id("Channel 0");
    m_controller->m_channel_id = 0;
    m_controller->m_clock_ratio = 1;

    gather_components();
  }

  void init() override {
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
  }

  bool send(Request req) override {
    return m_controller->send(req);
  }

  void tick() override {
    m_controller->tick();
  }

  int get_clock_ratio() override {
    return m_controller->m_clock_ratio;
  }

  float get_tCK() override {
    return m_controller->get_tCK();
  }

  int get_tx_bytes() override {
    return m_controller->get_tx_bytes();
  }

  IController* controller() const {
    return m_controller;
  }

  template <typename T>
  T* find_component() const {
    for (auto* component : m_components) {
      if (auto* typed = dynamic_cast<T*>(component)) {
        return typed;
      }
    }
    return nullptr;
  }

 protected:
  std::string get_name() const override {
    return "HarnessMemorySystem";
  }

  std::string get_ifce_name() const override {
    return IMemorySystem::get_name();
  }

 private:
  IController* m_controller = nullptr;
};

class ControllerUnderTestCpp {
 public:
  inline static constexpr int kHarnessInternalSourceId = -2;

  ControllerUnderTestCpp(nb::dict controller_config, int num_cores)
      : m_frontend(std::make_unique<HarnessFrontEnd>(num_cores)),
        m_memory_system(std::make_unique<HarnessMemorySystem>(
            inject_issued_command_validation_hook(py_to_confignode(controller_config)))) {
    m_frontend->connect_memory_system(m_memory_system.get());
    m_memory_system->connect_frontend(m_frontend.get());

    m_controller = m_memory_system->controller();
    m_controller_base = dynamic_cast<ControllerBase*>(m_controller);
    if (!m_controller_base) {
      throw std::runtime_error("ControllerUnderTest requires a ControllerBase-derived controller");
    }

    m_validation_hook = m_memory_system->find_component<IControllerValidationHook>();
    if (!m_validation_hook) {
      throw std::runtime_error("ControllerUnderTest could not find IssuedCommandValidationHook");
    }
  }

  std::vector<std::string> level_names() const {
    return spec().level_names;
  }

  std::vector<std::string> command_names() const {
    return spec().command_names;
  }

  std::map<std::string, int> timings() const {
    return timing_map(spec());
  }

  int timing(const std::string& name) const {
    return spec().get_timing_value(name);
  }

  void send_request(int type_id, const AddrVec_t& addr_vec, int source_id) {
    validate_concrete_addr_vec(addr_vec);
    Request req(addr_vec, type_id);
    req.addr = synthesize_addr(addr_vec);
    req.source_id = source_id;

    bool read_like = is_read_like_request(type_id);
    if (read_like) {
      m_read_completions_pending++;
      req.callback = [this](Request&) {
        if (m_read_completions_pending == 0) {
          throw std::runtime_error("ControllerUnderTest read completion accounting underflow");
        }
        m_read_completions_pending--;
      };
    }

    if (!m_controller->send(req)) {
      if (read_like) {
        m_read_completions_pending--;
      }
      throw std::runtime_error("ControllerUnderTest failed to enqueue request");
    }

    bool was_forwarded = read_like && req.depart != -1;
    if (!was_forwarded) {
      m_command_outstanding++;
    }
  }

  void priority_send(const std::string& command_name, const AddrVec_t& addr_vec) {
    validate_addr_vec_size(spec(), addr_vec);
    int command = spec().get_command_id(command_name);

    Request req(addr_vec, Request::Cmd, command);
    req.source_id = kHarnessInternalSourceId;
    if (!m_controller->priority_send(req)) {
      throw std::runtime_error("ControllerUnderTest failed to enqueue internal command");
    }
    m_command_outstanding++;
  }

  nb::list tick() {
    m_memory_system->tick();

    nb::list issued;
    for (const auto& rec : m_validation_hook->take_issued_commands_this_tick()) {
      bool tracked_final = rec.command == rec.final_command &&
                           (rec.type_id != -1 || rec.source_id == kHarnessInternalSourceId);
      if (tracked_final) {
        if (m_command_outstanding == 0) {
          throw std::runtime_error("ControllerUnderTest command accounting underflow");
        }
        m_command_outstanding--;
      }

      nb::dict item;
      item["clk"] = rec.clk;
      item["command"] = spec().command_names[rec.command];
      item["addr_vec"] = rec.addr_vec;
      item["type_id"] = rec.type_id;
      item["source_id"] = rec.source_id;
      issued.append(item);
    }

    return issued;
  }

  bool is_idle() const {
    return m_command_outstanding == 0 && m_read_completions_pending == 0;
  }

  nb::dict stats() {
    if (!m_stats_finalized) {
      m_memory_system->IMemorySystem::finalize();
      m_stats_finalized = true;
    }
    return nb::cast<nb::dict>(confignode_to_py(m_controller->collect_stats()));
  }

 private:
  std::unique_ptr<HarnessFrontEnd> m_frontend;
  std::unique_ptr<HarnessMemorySystem> m_memory_system;
  IController* m_controller = nullptr;
  ControllerBase* m_controller_base = nullptr;
  IControllerValidationHook* m_validation_hook = nullptr;
  size_t m_command_outstanding = 0;
  size_t m_read_completions_pending = 0;
  bool m_stats_finalized = false;

  const DRAMSpec& spec() const {
    return *m_controller_base->m_device.m_spec;
  }

  void validate_concrete_addr_vec(const AddrVec_t& addr_vec) const {
    validate_addr_vec_size(spec(), addr_vec);
    for (int level = 0; level < spec().level_count; level++) {
      if (addr_vec[level] < 0) {
        throw std::runtime_error("ControllerUnderTest.send_request requires a concrete addr_vec");
      }
    }
  }

  bool is_read_like_request(int type_id) const {
    return type_id == Request::Type::Read;
  }

  Addr_t synthesize_addr(const AddrVec_t& addr_vec) const {
    Addr_t addr = 0;
    for (int level = 0; level < spec().level_count; level++) {
      int count = spec().organization.level_sizes[level];
      if (count <= 0) {
        count = 1;
      }
      addr = addr * count + addr_vec[level];
    }
    return addr;
  }
};

// ---- nanobind module ----

NB_MODULE(_ramulator, m) {
  m.doc() = "Ramulator2 Python bindings";

  nb::class_<Simulation>(m, "Simulation")
      .def(nb::init<nb::dict>(), nb::arg("config"), "Create a simulation from a configuration dict.")
      .def("run", &Simulation::run, "Run the simulation to completion.")
      .def("get_stats", &Simulation::get_stats, "Finalize the simulation and return stats as a dict.")
      .def("get_stats_yaml", &Simulation::get_stats_yaml, "Finalize the simulation and return stats as a YAML string.");

  nb::class_<DeviceUnderTestCpp>(m, "_DeviceUnderTest")
      .def(nb::init<nb::dict>(), nb::arg("dram_config"))
      .def_prop_ro("level_names", &DeviceUnderTestCpp::level_names)
      .def_prop_ro("command_names", &DeviceUnderTestCpp::command_names)
      .def_prop_ro("timings", &DeviceUnderTestCpp::timings)
      .def("timing", &DeviceUnderTestCpp::timing, nb::arg("name"))
      .def("probe", &DeviceUnderTestCpp::probe, nb::arg("command"), nb::arg("addr_vec"), nb::arg("clk"))
      .def("issue", &DeviceUnderTestCpp::issue, nb::arg("command"), nb::arg("addr_vec"), nb::arg("clk"));

  nb::class_<ControllerUnderTestCpp>(m, "_ControllerUnderTest")
      .def(nb::init<nb::dict, int>(), nb::arg("controller_config"), nb::arg("num_cores") = 1)
      .def_prop_ro("level_names", &ControllerUnderTestCpp::level_names)
      .def_prop_ro("command_names", &ControllerUnderTestCpp::command_names)
      .def_prop_ro("timings", &ControllerUnderTestCpp::timings)
      .def("timing", &ControllerUnderTestCpp::timing, nb::arg("name"))
      .def("send_request", &ControllerUnderTestCpp::send_request,
           nb::arg("type_id"), nb::arg("addr_vec"), nb::arg("source_id") = 0)
      .def("priority_send", &ControllerUnderTestCpp::priority_send, nb::arg("command"), nb::arg("addr_vec"))
      .def("tick", &ControllerUnderTestCpp::tick)
      .def("is_idle", &ControllerUnderTestCpp::is_idle)
      .def("stats", &ControllerUnderTestCpp::stats);
}
