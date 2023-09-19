#include <iostream>

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "base/base.h"
#include "base/config.h"
#include "frontend/frontend.h"
#include "memory_system/memory_system.h"
#include "example/example_ifce.h"

int main(int argc, char* argv[]) {
  // Parse command line arguments
  argparse::ArgumentParser program("Ramulator", "2.0");
  program.add_argument("-c", "--config").metavar("\"dumped YAML configuration\"")
    .help("String dump of the yaml configuration.");
  program.add_argument("-f", "--config_file").metavar("path-to-configuration-file")
    .help("Path to a YAML configuration file.");
  program.add_argument("-p", "--param").metavar("KEY=VALUE")
    .append()
    .help("Specify parameter to override in the configuration file. Repeat this option to change multiple parameters.");

  try {
    program.parse_args(argc, argv);
  }
  catch (const std::runtime_error& err) {
    spdlog::error(err.what());
    std::cerr << program;
    std::exit(1);
  }

  // Are we accepting the configuration YAML through commandline dump?
  bool use_dumped_yaml = false;
  std::string dumped_config;
  if (auto arg = program.present<std::string>("-c")) {
    use_dumped_yaml = true;
    dumped_config = *arg;
  }

  // Are we gettign a path to a YAML document?
  bool use_yaml_file = false;
  std::string config_file_path;
  if (auto arg = program.present<std::string>("-f")) {
    use_yaml_file = true;
    config_file_path = *arg;
  }

  // Are we overriding some parameters in a YAML document from the comand line?
  bool has_param_override = false;
  std::vector<std::string> params;
  if (auto arg = program.present<std::vector<std::string>>("-p")) {
    has_param_override = true;
    params = *arg;
  }

  // Some sanity check of the inputs
  if (use_dumped_yaml && use_yaml_file) {
    spdlog::error("Dumped config and loaded config cannot be used together!");
    std::cerr << program;
    std::exit(1);
  } else if (!(use_dumped_yaml || use_yaml_file)) {
    spdlog::error("No configuration specified!");
    std::cerr << program;
    std::exit(1);
  }

  if (use_dumped_yaml && has_param_override) {
    spdlog::warn("Using dumped configuration. Parameter overrides with -p/--param will be ignored!");
  }
  
  // Parse the configurations
  YAML::Node config;
  if (use_dumped_yaml) {
    std::string dumped_config = program.get<std::string>("-c");
    config = YAML::Load(dumped_config);
  } else if (use_yaml_file) {
    config = Ramulator::Config::parse_config_file(config_file_path, params);
  }

  // Instaniate the frontend of the simulated system, this is one of the top-level objects in Ramulator 2.0.
  // It also recursively instaniate all components in the frontend.
  auto frontend = Ramulator::Factory::create_frontend(config);
  // Instaniate the memory system of the simulated system, this is one of the top-level objects in Ramulator 2.0
  // It also recursively instaniate all components in the memory system.
  auto memory_system = Ramulator::Factory::create_memory_system(config);

  // Connect the frontend and the memory system together,
  // this recursively calls the "setup" function in all instaniated components
  // so that they can get each other's parameters (if needed) after their initialization
  frontend->connect_memory_system(memory_system);
  memory_system->connect_frontend(frontend);

  // Get the relative clock ratio between the frontend and memory system
  int frontend_tick = frontend->get_clock_ratio();
  int mem_tick = memory_system->get_clock_ratio();

  int tick_mult = frontend_tick * mem_tick;

  for (uint64_t i = 0;; i++) {
    if (((i % tick_mult) % mem_tick) == 0) {
      frontend->tick();
    }

    if (frontend->is_finished()) {
      break;
    }

    if ((i % tick_mult) % frontend_tick == 0) {
      memory_system->tick();
    }
  }

  // Finalize the simulation. Recursively print all statistics from all components
  frontend->finalize();
  memory_system->finalize();

  return 0;
}