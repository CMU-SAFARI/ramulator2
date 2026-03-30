#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <fmt/format.h>
#include <memory>
#include <stdexcept>
#include <string>

#include "ramulator/base/factory.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/i_controller.h"
#include "ramulator/controller/plugin/controller_validation_hook.h"
#include "ramulator/dram/device.h"
#include "ramulator/dram/dram_spec.h"
#include "ramulator/frontend/i_frontend.h"
#include "ramulator/memory_system/i_memory_system.h"
#include "ramulator/python/binding_utils.h"

// ---- DeviceUnderTest ----

class DeviceUnderTestCpp {
 public:
  explicit DeviceUnderTestCpp(nb::dict dram_config) {
    ConfigNode cfg = py_to_confignode(dram_config);
    std::string dram_impl = cfg["impl"].as<std::string>();
    m_device.init(DRAMSpec::create(dram_impl, ConfigNode(ConfigNode::Map{{"dram", std::move(cfg)}})));
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

  void init() override {}
  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {}
  void tick() override {}
  bool is_finished() override { return false; }
  int get_num_cores() override { return m_num_cores; }

 protected:
  std::string get_name() const override { return "HarnessFrontEnd"; }
  std::string get_ifce_name() const override { return IFrontEnd::get_name(); }

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

  void init() override {}
  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {}

  bool send(Request& req) override {
    if (req.intra_channel_addr < 0) {
      req.intra_channel_addr = req.addr;
    }
    return m_controller->send(req);
  }

  void tick() override {
    m_controller->tick();
  }

  int get_clock_ratio() override { return m_controller->m_clock_ratio; }
  float get_tCK() override { return m_controller->get_tCK(); }
  int get_tx_bytes() override { return m_controller->get_tx_bytes(); }

  IController* controller() const { return m_controller; }

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
  std::string get_name() const override { return "HarnessMemorySystem"; }
  std::string get_ifce_name() const override { return IMemorySystem::get_name(); }

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

  std::vector<std::string> level_names() const { return spec().level_names; }
  std::vector<std::string> command_names() const { return spec().command_names; }
  std::map<std::string, int> timings() const { return timing_map(spec()); }

  int timing(const std::string& name) const {
    return spec().get_timing_value(name);
  }

  void send_request(int type_id, const AddrVec_t& addr_vec, int source_id) {
    validate_concrete_addr_vec(addr_vec);
    Request req(addr_vec, type_id);
    req.addr = synthesize_addr(addr_vec);
    req.intra_channel_addr = req.addr;
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
        throw std::runtime_error(fmt::format(
            "synthesize_addr: level {} has invalid size {}", level, count));
      }
      addr = addr * count + addr_vec[level];
    }
    return addr;
  }
};

// ---- nanobind module ----

NB_MODULE(_ramulator_test, m) {
  m.doc() = "Ramulator2 test harness bindings";

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
