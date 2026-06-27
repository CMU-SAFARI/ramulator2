#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <memory>
#include <sstream>
#include <stdexcept>

#include "ramulator/base/factory.h"
#include "ramulator/frontend/i_frontend.h"
#include "ramulator/memory_system/i_memory_system.h"
#include "ramulator/python/binding_utils.h"

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

    int fe_count = mem_tick - 1, mem_count = fe_tick - 1;
    for (;;) {
      if (++fe_count >= mem_tick) {
        fe_count = 0;
        m_frontend->tick();
      }

      if (m_frontend->is_finished()) {
        break;
      }

      if (++mem_count >= fe_tick) {
        mem_count = 0;
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

// ---- nanobind module ----

NB_MODULE(_ramulator, m) {
  m.doc() = "Ramulator2 Python bindings";

  nb::class_<Simulation>(m, "Simulation")
      .def(nb::init<nb::dict>(), nb::arg("config"), "Create a simulation from a configuration dict.")
      .def("run", &Simulation::run, "Run the simulation to completion.")
      .def("get_stats", &Simulation::get_stats, "Finalize the simulation and return stats as a dict.")
      .def("get_stats_yaml", &Simulation::get_stats_yaml, "Finalize the simulation and return stats as a YAML string.");
}
