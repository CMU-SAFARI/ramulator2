#include <fmt/format.h>

#include "ramulator/base/param.h"
#include "ramulator/controller/i_controller.h"
#include "ramulator/memory_system/channel_mapper/i_channel_mapper.h"
#include "ramulator/memory_system/i_memory_system.h"
#include "ramulator/translation/i_translation.h"

namespace Ramulator {

class GenericDRAMSystem final : public IMemorySystem, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IMemorySystem, GenericDRAMSystem, "GenericDRAM");

 protected:
  IChannelMapper* m_channel_mapper;
  std::vector<IController*> m_controllers;
  unsigned int m_clock_ratio = 1;

 public:
  int s_num_read_requests = 0;
  int s_num_write_requests = 0;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_clock_ratio, unsigned int, "clock_ratio").required();
    RAMULATOR_CREATE_CHILD(m_channel_mapper, IChannelMapper);

    // Each controller = one channel. DRAM config lives inside each controller.
    RAMULATOR_CREATE_CHILD_LIST(m_controllers, IController);
    if (m_controllers.empty()) {
      throw std::runtime_error("GenericDRAM requires at least one controller");
    }
    for (size_t i = 0; i < m_controllers.size(); i++) {
      dynamic_cast<Implementation*>(m_controllers[i])->set_id(fmt::format("Channel {}", i));
      m_controllers[i]->m_channel_id = static_cast<int>(i);
      m_controllers[i]->m_clock_ratio = m_clock_ratio;
    }

    // Setup channel mapper with controller info
    int tx_bytes = m_controllers[0]->get_tx_bytes();
    m_channel_mapper->setup(static_cast<int>(m_controllers.size()), calc_log2(tx_bytes));

    m_stats.add("total_num_read_requests", s_num_read_requests);
    m_stats.add("total_num_write_requests", s_num_write_requests);
  };

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
  }

  bool send(Request req) override {
    int channel_id = m_channel_mapper->get_channel(req.addr);
    Addr_t stripped = m_channel_mapper->get_intra_channel_addr(req.addr);
    m_controllers[channel_id]->apply_mapping(stripped, req);
    req.addr_vec[0] = channel_id;
    bool is_success = m_controllers[channel_id]->send(req);

    if (is_success) {
      switch (req.type_id) {
        case Request::Type::Read: {
          s_num_read_requests++;
          break;
        }
        case Request::Type::Write: {
          s_num_write_requests++;
          break;
        }
      }
    }

    return is_success;
  };

  void tick() override {
    for (auto controller : m_controllers) {
      controller->tick();
    }
  };

  int get_clock_ratio() override {
    return m_clock_ratio;
  }

  float get_tCK() override {
    if (!m_controllers.empty()) {
      return m_controllers[0]->get_tCK();
    }
    return -1.0f;
  }
};

}  // namespace Ramulator
