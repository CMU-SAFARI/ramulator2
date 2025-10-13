#include "memory_system/memory_system.h"
#include "translation/translation.h"
#include "dram_controller/controller.h"
#include "addr_mapper/addr_mapper.h"
#include "dram/dram.h"

namespace Ramulator {

class GenericDRAMSystem final : public IMemorySystem, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IMemorySystem, GenericDRAMSystem, "GenericDRAM", "A generic DRAM-based memory system.");

  protected:
    Clk_t m_clk = 0;
    IDRAM*  m_dram;
    IAddrMapper*  m_addr_mapper;
    std::vector<IDRAMController*> m_controllers;

  public:
    int s_num_read_requests = 0;
    int s_num_write_requests = 0;
    int s_num_other_requests = 0;


  public:
    // tells the on-the-fly request generator when all requests have been issued by controller
    bool is_finished_ms() override {
      for (auto controller : m_controllers) {
        size_t rq = controller->get_read_queue_length();
        size_t wq = controller->get_write_queue_length();
        size_t ab = controller->get_active_buffer_length(); 
        //std::cout << "rq: " << rq << ", wq" << wq << std::endl;
        if (rq!=0 || wq!=0 || ab!=0) {
          return false; 
        } 
      }
      return true;
    }

    bool is_request_finished(Request req) override {
      bool in_read = false;
      bool in_active = false;
      for (auto controller: m_controllers) {
        in_read = controller->is_req_in_read_queue(req);
        in_active = controller->is_req_in_pending_queue(req);
        if (in_read || in_active) return false;
      }
      return !(in_read || in_active);
    }


    int get_total_address_bits() override {
      int num_row_bits = calc_log2(m_dram->get_level_size("row"));
      int num_col_bits = calc_log2(m_dram->get_level_size("column"));
      num_col_bits -= calc_log2(m_dram->m_internal_prefetch_size);
      int num_bank_bits = calc_log2(m_dram->get_level_size("bank"));
      int num_bankgroup_bits = calc_log2(m_dram->get_level_size("bankgroup"));
      int num_rank_bits = calc_log2(m_dram->get_level_size("rank"));
      int num_channel_bits = calc_log2(m_dram->get_level_size("channel"));

      int tx_bytes = m_dram->m_internal_prefetch_size * m_dram->m_channel_width / 8;
      int tx_offset = calc_log2(tx_bytes);

      int total_bits = num_channel_bits + num_rank_bits + num_bankgroup_bits + num_bank_bits + num_row_bits + num_col_bits + tx_offset;
      return total_bits;
    }

    int get_shift_amt(int index) override {
      // 0 = bankgroup, 1 = bank, 2 = rank, 3 = row, 4 = col, 5 = channel
      int num_row_bits = calc_log2(m_dram->get_level_size("row"));
      int num_col_bits = calc_log2(m_dram->get_level_size("column"));
      num_col_bits -= calc_log2(m_dram->m_internal_prefetch_size);
      int num_bank_bits = calc_log2(m_dram->get_level_size("bank"));
      int num_bankgroup_bits = calc_log2(m_dram->get_level_size("bankgroup"));
      int num_rank_bits = calc_log2(m_dram->get_level_size("rank"));
      int num_channel_bits = calc_log2(m_dram->get_level_size("channel"));

      int tx_bytes = m_dram->m_internal_prefetch_size * m_dram->m_channel_width / 8;
      int tx_offset = calc_log2(tx_bytes);
      switch (index) {
        case 0: // rank
          return num_bankgroup_bits + num_bank_bits + num_row_bits + num_col_bits + tx_offset;
        case 1: // bankgroup
          return num_bank_bits + num_row_bits + num_col_bits + tx_offset;
        case 2: // bank
          return num_row_bits + num_col_bits + tx_offset;
        case 3: // row
           return num_col_bits + tx_offset;
        case 4: // col
           return tx_offset;
        case 5: // channel
          return num_rank_bits + num_bankgroup_bits + num_bank_bits + num_row_bits + num_col_bits + tx_offset;
        default: return -1;
      }
    }

    size_t get_max(int index) override {
      // 0 = bankgroup, 1 = bank, 2 = rank, 3 = row, 4 = col
      int num_row_bits = calc_log2(m_dram->get_level_size("row"));
      int num_col_bits = calc_log2(m_dram->get_level_size("column"));
      num_col_bits -= calc_log2(m_dram->m_internal_prefetch_size);
      int num_bank_bits = calc_log2(m_dram->get_level_size("bank"));
      int num_bankgroup_bits = calc_log2(m_dram->get_level_size("bankgroup"));
      int num_rank_bits = calc_log2(m_dram->get_level_size("rank"));
      int num_channel_bits = calc_log2(m_dram->get_level_size("channel"));

      int tx_bytes = m_dram->m_internal_prefetch_size * m_dram->m_channel_width / 8;
      int tx_offset = calc_log2(tx_bytes);
      switch (index) {
        case 0: // rank
          return num_bankgroup_bits + num_bank_bits + num_rank_bits + num_row_bits + num_col_bits + tx_offset;
        case 1: // bankgroup
          return num_bankgroup_bits + num_bank_bits + num_row_bits + num_col_bits + tx_offset;
        case 2: // bank
          return num_bank_bits + num_row_bits + num_col_bits + tx_offset;
        case 3: // row
           return num_row_bits + num_col_bits + tx_offset;
        case 4: // col
           return num_col_bits + tx_offset;
        case 5: // channel
          return num_channel_bits + num_rank_bits + num_bankgroup_bits + num_bank_bits + num_row_bits + num_col_bits + tx_offset;
        default: return -1;
      }
    }

    int get_num_channels() override {
      return m_dram->get_level_size("channel");
    }

    void init() override { 
      // Create device (a top-level node wrapping all channel nodes)
      m_dram = create_child_ifce<IDRAM>();
      m_addr_mapper = create_child_ifce<IAddrMapper>();

      int num_channels = m_dram->get_level_size("channel");   

      // Create memory controllers
      for (int i = 0; i < num_channels; i++) {
        IDRAMController* controller = create_child_ifce<IDRAMController>();
        controller->m_impl->set_id(fmt::format("Channel {}", i));
        controller->m_channel_id = i;
        m_controllers.push_back(controller);
      }

      m_clock_ratio = param<uint>("clock_ratio").required();

      register_stat(m_clk).name("memory_system_cycles");
      register_stat(s_num_read_requests).name("total_num_read_requests");
      register_stat(s_num_write_requests).name("total_num_write_requests");
      register_stat(s_num_other_requests).name("total_num_other_requests");
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override { }

    bool send(Request req) override {
      m_addr_mapper->apply(req);
      int channel_id = req.addr_vec[0];
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
          default: {
            s_num_other_requests++;
            break;
          }
        }
      }

      return is_success;
    };
    
    void tick() override {
      m_clk++;
      m_dram->tick();
      for (auto controller : m_controllers) {
        controller->tick();
      }
    };

    float get_tCK() override {
      return m_dram->m_timing_vals("tCK_ps") / 1000.0f;
    }

    // const SpecDef& get_supported_requests() override {
    //   return m_dram->m_requests;
    // };
};
  
}   // namespace 

