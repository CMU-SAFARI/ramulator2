#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <bitset>

#include "base/exception.h"
#include "base/request.h"
#include "base/type.h"
#include "frontend/frontend.h"

namespace Ramulator {
class MessReqGenerator : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, MessReqGenerator, "MessReqGenerator", "Sequential Random accesses and Stream traces.")

 private:
  std::string mess_trace_path;
  size_t m_total_requests;
  size_t m_issued_requests;
  size_t s_retried_requests;

  size_t s_random_reads; 
  size_t s_random_writes;
  size_t s_stride_reads;
  size_t s_stride_writes;
  size_t s_total_numer_of_idle_ticks;
  size_t s_total_number_for_idle_ticks_random_reads;

  // stores the shift amount we need to increment by
  // rank, bankgroup, bank, column, row, channel
  int shift_amts[6] = {-1, -1, -1, -1, -1, -1};
  int64_t max_val[6] = {-1, -1, -1, -1, -1, -1};
  std::vector<int64_t> channel_offsets;
  std::vector<Addr_t> stream_addrs;
  int addr_index;
  int64_t m_num_columns;  // the number of columns we iterate over for STREAM

  size_t random_count;
  bool m_issue_random;
  float m_read_ratio;

  bool m_disable_random;

  bool retry_send_stream;
  bool retry_send_random;
  Request m_curr_stride_req = {0, 0};
  Request m_curr_random_req = {0, 0};
  Request m_last_random_req = {-1, 0};

  struct Trace {
    bool is_write;
    Addr_t addr;
  };

  Logger_t m_logger;

  Addr_t m_min_addr;
  int64_t m_max_addr;

  size_t m_nop_counter;
  size_t m_curr_nop_counter;

  std::mt19937 m_gen;
  std::bernoulli_distribution m_dist_read_write;

  size_t m_frontend_ticks = 0;

  bool is_write() {
    bool is_read = m_dist_read_write(m_gen);
    return !is_read;
  }

  Request get_random_request() {
    if (m_max_addr == -1) {
      set_max_addr();
    }
    static std::random_device rd;
    static std::mt19937 gen(rd());
    //std::cout << "get random req, max: " << m_max_addr << ", min: " << m_min_addr << std::endl;
    std::uniform_int_distribution<uint64_t> dist(m_min_addr, m_max_addr);
    Addr_t addr = dist(gen);
    Request res = {addr, false};  // pointer chase just does reads
    res.request_type = 0;  // set type id to 0 for random request
    m_curr_random_req = res;
    return res;
  }

  int64_t get_max_addr() {
    int total_bits = m_memory_system->get_total_address_bits();
    // we will be issueing the request to all channels, so we need to subtract the channel bits
    total_bits -= std::log2(m_memory_system->get_num_channels());
    return (1LL << total_bits) - 1;
  }

  void set_max_addr() {
    m_max_addr = get_max_addr();
    if (m_max_addr <= m_min_addr) {
      throw ConfigurationError("Invalid address range: max_addr must be > min_addr");
    }
  }

  void set_up_channel_offsets() {
    int num_channels = m_memory_system->get_num_channels();
    channel_offsets.resize(num_channels);
    channel_offsets[0] = 0;
    for (int i = 0; i < num_channels; i++) {
      channel_offsets[i] = (int64_t)i << m_memory_system->get_shift_amt(5);
    }
    //print channel offsets
    for(int i=0; i<num_channels; i++) {
      std::cout << "channel " << i << " offset: " << std::bitset<40>(channel_offsets[i]) << std::endl;
    }
    
  }

  void setup_shift_amts() {
    for (int i=0; i< 6; i++) {
      // column and row are reversed: 
      if (i == 3) { // col
        shift_amts[i] = m_memory_system->get_shift_amt(4);
        max_val[i] = (1 << m_memory_system->get_max(4)) -1;
      } else if (i == 4) {  // row
        shift_amts[i] = m_memory_system->get_shift_amt(3);
        max_val[i] = (1 << m_memory_system->get_max(3)) -1;
      } else {
        // get the shift amount you need to increase by to increment LSB
        shift_amts[i] = m_memory_system->get_shift_amt(i);
        // get mask for max value
        max_val[i] = (1LL << m_memory_system->get_max(i)) -1;
      }
    }
    // print shift amts
    /*
    std::cout << "shift amts: ";
    for (int i=0; i<6; i++) {
      std::cout << shift_amts[i] << ", "; 
    }
    std::cout << std::endl;
    */
  }

  enum AddrFields {
    rank = 0,
    bankgroup = 1,
    bank = 2,
    column = 3,
    row = 4,
    channel = 5
  };

  void setup_stream_addresses() {
    setup_shift_amts();
    set_up_channel_offsets();
    Addr_t addr = 0;
    for (int64_t i=0; i < max_val[row]; i += (1LL << shift_amts[row])) {
      for (int64_t j = 0, s = 0; s < m_num_columns; j += (1LL << shift_amts[column]), s++) { // limit to 8 columns for now -> todo: make configurable
        for (int64_t k = 0; k < max_val[bank]; k += (1LL << shift_amts[bank])) {
          for (int64_t l = 0; l < max_val[bankgroup]; l += (1LL << shift_amts[bankgroup])) {
            // for (int64_t n = 0; n < max_val[rank]; n += (1LL << shift_amts[rank])) {
              //for (int64_t m = 0; m < max_val[channel]; m += (1LL << shift_amts[channel])) {
                addr = addr + i + j + k + l; //+ n; // + m;
                //std::cout << "max bankgroup " << max_val[bankgroup] << ", shift " << shift_amts[bankgroup] << std::endl;
                //std::cout << "max channel " << max_val[channel] << ", shift " << shift_amts[channel] << std::endl; 
                //std::cout << "stream addr: " << std::bitset<36>(addr) << std::endl;
                stream_addrs.push_back(addr);
                addr = 0;
              //}
            // }
          }
        }
      }
    }
  }

  Request get_next_stream_request() {
    if (m_max_addr == -1) {
      set_max_addr();
    }
    if (stream_addrs.size() == 0) setup_stream_addresses();
    Addr_t addr = stream_addrs[addr_index];
    //Addr_t addr = 69;
    addr_index = (addr_index + 1) % stream_addrs.size();
    bool is_read = m_dist_read_write(m_gen);
    Request res = {addr, is_write()};
    res.request_type = 1; // set type id to 1 for strided request
    m_curr_stride_req = res;
    return res;
  }

 public:
  void init() override {
    m_logger = Logging::create_logger("OnTheFlyReqGenerator");
    m_clock_ratio = param<uint>("clock_ratio").required();
    m_total_requests = param<uint>("total_requests").required();
    m_min_addr = param<Addr_t>("start_addr").required();
    m_max_addr = -1; // will be initialized when memory system is connected
    m_read_ratio = param<float>("ratio_reads").default_val(0.5);
    m_disable_random = param<bool>("disable_random").default_val(false);
    if (m_read_ratio > 1 || m_read_ratio < 0) {
      throw ConfigurationError("Read ration must be between 0 and 1!");
    }
    m_issued_requests = 0;
    m_issue_random = false;
    retry_send_stream = false;
    retry_send_random = false;
    m_nop_counter = param<uint>("nop_counter").default_val(1);
    m_curr_nop_counter = 0;

    s_retried_requests = 0; 
    s_random_reads = 0; 
    s_random_writes = 0; 
    s_stride_reads = 0;
    s_stride_writes = 0;
    s_total_numer_of_idle_ticks = 0;
    s_total_number_for_idle_ticks_random_reads = 0;
    m_num_columns = param<int64_t>("num_columns").default_val(8);
    addr_index = 0;

    std::seed_seq seed{342};
    m_gen = std::mt19937(seed);
    m_dist_read_write = std::bernoulli_distribution(m_read_ratio);
    register_stat(m_total_requests).name("total_number_of_issued_requests");
    register_stat(s_retried_requests).name("total_number_of_retried_requests");
    register_stat(s_random_reads).name("total_number_of_random_reads");
    register_stat(s_random_writes).name("total_number_of_random_writes");
    register_stat(s_stride_reads).name("total_number_of_stride_reads");
    register_stat(s_stride_writes).name("total_number_of_stride_writes");
    register_stat(m_read_ratio).name("read_ratio");
    register_stat(m_nop_counter).name("nop_counter_value");
    register_stat(s_total_numer_of_idle_ticks).name("s_total_numer_of_idle_ticks");
    register_stat(s_total_number_for_idle_ticks_random_reads).name("s_total_number_for_idle_ticks_random_reads");
    register_stat(m_max_addr).name("max_address");
    register_stat(m_num_columns).name("num_columns");

    random_count = 0;
  };


  void update_stats() {
    if (m_curr_random_req.type_id == Request::Type::Read && !m_issue_random) {
      s_random_reads ++;
    } else if (m_curr_stride_req.type_id == Request::Type::Read && m_issue_random) {
      s_stride_reads ++;
    } else if (m_curr_random_req.type_id == Request::Type::Write && !m_issue_random) {
      s_random_writes ++;
    } else {
      s_stride_writes ++;
    }
  }

  // sends a requests to all channels 
  // by adding the channel offset to the address
  bool send_request(Request req) {
    bool sent = false;
    for (int i=0; i<channel_offsets.size(); i++) {
      Request req_i = req;
      req_i.addr += channel_offsets[i];
      sent = m_memory_system->send(req_i);
    }
    return sent;
  }
  
  bool issue_request() {
    bool sent = false;
    if (m_issue_random) {
      Request req = {0, 0};
      if (retry_send_random) {
        req = m_curr_random_req;
      } else {
        req = get_random_request();
      }

      if (m_disable_random)
        sent = true;
      else
        sent = send_request(req);
    
      if (sent) {  // request accepted by MC
        random_count += 1;
        m_last_random_req = req;
        m_issue_random = !m_issue_random;
        retry_send_random = false;
      } else {  // request rejected by MC -> we need to retry sending it
        retry_send_random = true;
      }
      
    } else {  // issue stream
      Request req = {0, 0};
      if (retry_send_stream) {
        req = m_curr_stride_req;
      } else {  // issue new request
        req = get_next_stream_request();
      }
      sent = send_request(req);
      if (sent) {
        retry_send_stream = false;
        m_issue_random = !m_issue_random;
      } else {
        retry_send_stream = true;
      }
    }
    return sent;
  }

  // regulate the bandwidth by inserting a number of NOP operations
  // is this function returns true, no operations are issued that cycle
  bool is_idle_tick() {
    bool res = true;
    if ((m_curr_nop_counter == 0) || m_issue_random) res = false;  // if we are mod NOP, or issueing a random req, not idle
    //if (m_curr_nop_counter == 0) res = false;                        // insert NOPs for both stream and ptr-chase
    if (!m_issue_random) {
      // if we are in stream mode, increment the NOP counter
      m_curr_nop_counter = (m_curr_nop_counter + 1) % m_nop_counter;
    }
    if (res) s_total_numer_of_idle_ticks ++;
    return res;
  }

  bool can_issue_random_req() {
    if (m_last_random_req.addr == -1) 
      return true;
    else if (m_memory_system->is_request_finished(m_last_random_req)) 
      return true;
    return false;
  }

  void tick() override {
    // add this line to only issue stream requests -> must terminate run manually & stats are wrong btw
    //m_issue_random = false;
    if ((m_frontend_ticks % 100000) == 0) {
      std::cout << "Frontend: tick " << m_frontend_ticks << ", issued " << random_count << "/" << m_total_requests << "random requests" << std::endl;
    }
    m_frontend_ticks++;

    bool idle = is_idle_tick();
    //if ((m_issued_requests >= m_total_requests) || idle) {
    if (idle || (random_count >= m_total_requests)) {
      m_issue_random = !m_issue_random;
      return;
    }

    // make sure random requests are issued sequentially
    if (m_issue_random && !can_issue_random_req()) {
      //std::cout << "waiting for req to addr " << m_last_random_req.addr << " to finish" << std::endl;
      s_total_number_for_idle_ticks_random_reads ++;
      m_issue_random = false;
      return;
    }
      
    bool request_sent = issue_request();
    if (request_sent) {
      m_issued_requests++;
      update_stats(); 
    } else {
      s_retried_requests ++; 
    }
  };

  //bool is_finished() override { return (m_issued_requests >= m_total_requests && m_memory_system->is_finished_ms()); };

  // alternative is_finished that looks at only random request counts
  bool is_finished() override { return (random_count >= m_total_requests && m_memory_system->is_finished_ms()); };
};
}  // end namespace Ramulator