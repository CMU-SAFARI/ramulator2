#include "dram/dram.h"
#include "dram/lambdas.h"

namespace Ramulator {

class DDR3 : public IDRAM, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IDRAM, DDR3, "DDR3", "DDR3 Device Model")

  public:
    inline static const std::map<std::string, Organization> org_presets = {
      //   name         density  DQ   Ch Ra Ba   Ro     Co
      {"DDR3_1Gb_x4",   {1<<10,  4,  {1, 1, 8, 1<<14, 1<<11}}},
      {"DDR3_1Gb_x8",   {1<<10,  8,  {1, 1, 8, 1<<14, 1<<10}}},
      {"DDR3_1Gb_x16",  {1<<10,  16, {1, 1, 8, 1<<13, 1<<10}}},
      {"DDR3_2Gb_x4",   {2<<10,  4,  {1, 1, 8, 1<<15, 1<<11}}},
      {"DDR3_2Gb_x8",   {2<<10,  8,  {1, 1, 8, 1<<15, 1<<10}}},
      {"DDR3_2Gb_x16",  {2<<10,  16, {1, 1, 8, 1<<14, 1<<10}}},
      {"DDR3_4Gb_x4",   {4<<10,  4,  {1, 1, 8, 1<<16, 1<<11}}},
      {"DDR3_4Gb_x8",   {4<<10,  8,  {1, 1, 8, 1<<16, 1<<10}}},
      {"DDR3_4Gb_x16",  {4<<10,  16, {1, 1, 8, 1<<15, 1<<10}}},
      {"DDR3_8Gb_x4",   {8<<10,  4,  {1, 1, 8, 1<<17, 1<<11}}},
      {"DDR3_8Gb_x8",   {8<<10,  8,  {1, 1, 8, 1<<17, 1<<10}}},
      {"DDR3_8Gb_x16",  {8<<10,  16, {1, 1, 8, 1<<16, 1<<10}}},
    };

    inline static const std::map<std::string, std::vector<int>> timing_presets = {
      //   name       rate    nBL  nCL  nRCD  nRP   nRAS  nRC   nWR  nRTP nCWL nCCD  nRRD  nWTR  nFAW  nRFC nREFI  nCS  tCK_ps
      {"DDR3_800D",   {800,    4,   5,   5,    5,    15,  20,    6,   4,   9,    4,   -1,    4,   -1,  -1,   -1,    2,  2500}},
      {"DDR3_800E",   {800,    4,   5,   5,    5,    15,  20,    6,   4,   9,    4,   -1,    4,   -1,  -1,   -1,    2,  2500}},
      {"DDR3_1066E",  {1066,   4,   6,   6,    6,    20,  26,    8,   4,   9,    4,   -1,    4,   -1,  -1,   -1,    2,  1875}},
      {"DDR3_1066F",  {1066,   4,   7,   7,    7,    20,  27,    8,   4,   9,    4,   -1,    4,   -1,  -1,   -1,    2,  1875}},
      {"DDR3_1066G",  {1066,   4,   8,   8,    8,    20,  28,    8,   4,   9,    4,   -1,    4,   -1,  -1,   -1,    2,  1875}},
      {"DDR3_1333G",  {1333,   4,   8,   8,    8,    24,  32,   10,   5,   9,    4,   -1,    5,   -1,  -1,   -1,    2,  1500}},
      {"DDR3_1333H",  {1333,   4,   9,   9,    9,    24,  33,   10,   5,   9,    4,   -1,    5,   -1,  -1,   -1,    2,  1500}},
      {"DDR3_1600H",  {1600,   4,   9,   9,    9,    28,  37,   12,   6,   9,    4,   -1,    6,   -1,  -1,   -1,    2,  1250}},
      {"DDR3_1600J",  {1600,   4,  10,  10,   10,    28,  38,   12,   6,   9,    4,   -1,    6,   -1,  -1,   -1,    2,  1250}},
      {"DDR3_1600K",  {1600,   4,  11,  11,   11,    28,  39,   12,   6,   9,    4,   -1,    6,   -1,  -1,   -1,    2,  1250}},
      {"DDR3_1866K",  {1866,   4,  11,  11,   11,    32,  43,   14,   7,   9,    4,   -1,    7,   -1,  -1,   -1,    2,  1071}},
      {"DDR3_1866L",  {1866,   4,  12,  12,   12,    32,  44,   14,   7,   9,    4,   -1,    7,   -1,  -1,   -1,    2,  1071}},
      {"DDR3_2133L",  {2133,   4,  12,  12,   12,    36,  48,   16,   8,   9,    4,   -1,    8,   -1,  -1,   -1,    2,  937}},
      {"DDR3_2133M",  {2133,   4,  13,  13,   13,    36,  49,   16,   8,   9,    4,   -1,    8,   -1,  -1,   -1,    2,  937}},
    };

  /************************************************
   *                Organization
   ***********************************************/   
    const int m_internal_prefetch_size = 8;

    inline static constexpr ImplDef m_levels = {
      "channel", "rank", "bank", "row", "column",    
    };


  /************************************************
   *             Requests & Commands
   ***********************************************/
    inline static constexpr ImplDef m_commands = {
      "ACT", 
      "PRE", "PREA",
      "RD",  "WR",  "RDA",  "WRA",
      "REFab",
    };

    inline static const ImplLUT m_command_scopes = LUT (
      m_commands, m_levels, {
        {"ACT",   "row"},
        {"PRE",   "bank"},   {"PREA",   "rank"},
        {"RD",    "column"}, {"WR",     "column"}, {"RDA",   "column"}, {"WRA",   "column"},
        {"REFab", "rank"},
      }
    );

    inline static const ImplLUT m_command_meta = LUT<DRAMCommandMeta> (
      m_commands, {
                // open?   close?   access?  refresh?
        {"ACT",   {true,   false,   false,   false}},
        {"PRE",   {false,  true,    false,   false}},
        {"PREA",  {false,  true,    false,   false}},
        {"RD",    {false,  false,   true,    false}},
        {"WR",    {false,  false,   true,    false}},
        {"RDA",   {false,  true,    true,    false}},
        {"WRA",   {false,  true,    true,    false}},
        {"REFab", {false,  false,   false,   true }},
      }
    );

    inline static constexpr ImplDef m_requests = {
      "read", "write", "all-bank-refresh",
    };

    inline static const ImplLUT m_request_translations = LUT (
      m_requests, m_commands, {
        {"read", "RD"}, {"write", "WR"}, {"all-bank-refresh", "REFab"},
      }
    );

   
  /************************************************
   *                   Timing
   ***********************************************/
    inline static constexpr ImplDef m_timings = {
      "rate", 
      "nBL", "nCL", "nRCD", "nRP", "nRAS", "nRC", "nWR", "nRTP", "nCWL",
      "nCCD",
      "nRRD",
      "nWTR",
      "nFAW",
      "nRFC", "nREFI",
      "nCS",
      "tCK_ps"
    };


  /************************************************
   *                 Node States
   ***********************************************/
    inline static constexpr ImplDef m_states = {
       "Opened", "Closed", "PowerUp", "N/A", "Refreshing"
    };

    inline static const ImplLUT m_init_states = LUT (
      m_levels, m_states, {
        {"channel",   "N/A"}, 
        {"rank",      "PowerUp"},
        {"bank",      "Closed"},
        {"row",       "Closed"},
        {"column",    "N/A"},
      }
    );

  public:
    struct Node : public DRAMNodeBase<DDR3> {
      Node(DDR3* dram, Node* parent, int level, int id) : DRAMNodeBase<DDR3>(dram, parent, level, id) {};
    };
    std::vector<Node*> m_channels;
    
    FuncMatrix<ActionFunc_t<Node>>  m_actions;
    FuncMatrix<PreqFunc_t<Node>>    m_preqs;
    FuncMatrix<RowhitFunc_t<Node>>  m_rowhits;
    FuncMatrix<RowopenFunc_t<Node>> m_rowopens;


  public:
    void tick() override {
      m_clk++;
    };

    void init() override {
      RAMULATOR_DECLARE_SPECS();
      set_organization();
      set_timing_vals();

      set_actions();
      set_preqs();
      set_rowhits();
      set_rowopens();
      
      create_nodes();
    };

    void issue_command(int command, const AddrVec_t& addr_vec) override {
      int channel_id = addr_vec[m_levels["channel"]];
      m_channels[channel_id]->update_timing(command, addr_vec, m_clk);
      m_channels[channel_id]->update_states(command, addr_vec, m_clk);
    };

    int get_preq_command(int command, const AddrVec_t& addr_vec) override {
      int channel_id = addr_vec[m_levels["channel"]];
      return m_channels[channel_id]->get_preq_command(command, addr_vec, m_clk);
    };

    bool check_ready(int command, const AddrVec_t& addr_vec) override {
      int channel_id = addr_vec[m_levels["channel"]];
      return m_channels[channel_id]->check_ready(command, addr_vec, m_clk);
    };

    bool check_rowbuffer_hit(int command, const AddrVec_t& addr_vec) override {
      int channel_id = addr_vec[m_levels["channel"]];
      return m_channels[channel_id]->check_rowbuffer_hit(command, addr_vec, m_clk);
    };
    
    bool check_node_open(int command, const AddrVec_t& addr_vec) override {
      int channel_id = addr_vec[m_levels["channel"]];
      return m_channels[channel_id]->check_node_open(command, addr_vec, m_clk);
    };

  private:
    void set_organization() {
      // Channel width
      m_channel_width = param_group("org").param<int>("channel_width").default_val(64);

      // Organization
      m_organization.count.resize(m_levels.size(), -1);

      // Load organization preset if provided
      if (auto preset_name = param_group("org").param<std::string>("preset").optional()) {
        if (org_presets.count(*preset_name) > 0) {
          m_organization = org_presets.at(*preset_name);
        } else {
          throw ConfigurationError("Unrecognized organization preset \"{}\" in {}!", *preset_name, get_name());
        }
      }

      // Override the preset with any provided settings
      if (auto dq = param_group("org").param<int>("dq").optional()) {
        m_organization.dq = *dq;
      }

      for (int i = 0; i < m_levels.size(); i++){
        auto level_name = m_levels(i);
        if (auto sz = param_group("org").param<int>(level_name).optional()) {
          m_organization.count[i] = *sz;
        }
      }

      if (auto density = param_group("org").param<int>("density").optional()) {
        m_organization.density = *density;
      }

      // Sanity check: is the calculated chip density the same as the provided one?
      size_t _density = size_t(m_organization.count[m_levels["bank"]]) *
                        size_t(m_organization.count[m_levels["row"]]) *
                        size_t(m_organization.count[m_levels["column"]]) *
                        size_t(m_organization.dq);
      _density >>= 20;
      if (m_organization.density != _density) {
        throw ConfigurationError(
            "Calculated {} chip density {} Mb does not equal the provided density {} Mb!", 
            get_name(),
            _density, 
            m_organization.density
        );
      }

    };

    void set_timing_vals() {
      m_timing_vals.resize(m_timings.size(), -1);

      // Load timing preset if provided
      bool preset_provided = false;
      if (auto preset_name = param_group("timing").param<std::string>("preset").optional()) {
        if (timing_presets.count(*preset_name) > 0) {
          m_timing_vals = timing_presets.at(*preset_name);
          preset_provided = true;
        } else {
          throw ConfigurationError("Unrecognized timing preset \"{}\" in {}!", *preset_name, get_name());
        }
      }

      // Check for rate (in MT/s), and if provided, calculate and set tCK (in picosecond)
      if (auto dq = param_group("timing").param<int>("rate").optional()) {
        if (preset_provided) {
          throw ConfigurationError("Cannot change the transfer rate of {} when using a speed preset !", get_name());
        }
        m_timing_vals("rate") = *dq;
      }
      int tCK_ps = 1E6 / (m_timing_vals("rate") / 2);
      m_timing_vals("tCK_ps") = tCK_ps;

      // Load the organization specific timings
      int dq_id = [](int dq) -> int {
        switch (dq) {
          case 4:  return 0;
          case 8:  return 1;
          case 16: return 2;
          default: return -1;
        }
      }(m_organization.dq);

      int rate_id = [](int rate) -> int {
        switch (rate) {
          case  800:  return 0;
          case 1066:  return 1;
          case 1333:  return 2;
          case 1600:  return 3;
          case 1866:  return 4;
          case 2133:  return 5;
          default:    return -1;
        }
      }(m_timing_vals("rate"));

      constexpr int nRRD_TABLE[3][6] = {
      // 800   1066  1333  1600  1866  2133 
        { 4,    4,    4,    5,    5,    6},   // x4
        { 4,    4,    4,    5,    5,    6},   // x8
        { 4,    6,    5,    6,    6,    7},   // x16
      };
      constexpr int nFAW_TABLE[3][6] = {
      // 800   1066  1333  1600  1866  2133 
        { 16,   20,   20,   24,   26,   27},  // x4
        { 16,   20,   20,   24,   26,   27},  // x8
        { 20,   27,   30,   32,   33,   34},  // x16
      };

      if (dq_id != -1 && rate_id != -1) {
        m_timing_vals("nRRD") = nRRD_TABLE[dq_id][rate_id];
        m_timing_vals("nFAW") = nFAW_TABLE [dq_id][rate_id];
      }

      // Refresh timings
      // tRFC table (unit is nanosecond!)
      constexpr int tRFC_TABLE[4] = {
      // 1Gb   2Gb   4Gb   8Gb
         110,  160,  260,  350,
      };

      // tREFI(base) table (unit is nanosecond!)
      constexpr int tREFI_BASE = 7800;
      int density_id = [](int density_Mb) -> int { 
        switch (density_Mb) {
          case 1024:  return 0;
          case 2048:  return 1;
          case 4096:  return 2;
          case 8192:  return 3;
          default:    return -1;
        }
      }(m_organization.density);

      m_timing_vals("nRFC")  = JEDEC_rounding(tRFC_TABLE[density_id], tCK_ps);
      m_timing_vals("nREFI") = JEDEC_rounding(tREFI_BASE, tCK_ps);

      // Overwrite timing parameters with any user-provided value
      // Rate and tCK should not be overwritten
      for (int i = 1; i < m_timings.size() - 1; i++) {
        auto timing_name = std::string(m_timings(i));

        if (auto provided_timing = param_group("timing").param<int>(timing_name).optional()) {
          // Check if the user specifies in the number of cycles (e.g., nRCD)
          m_timing_vals(i) = *provided_timing;
        } else if (auto provided_timing = param_group("timing").param<float>(timing_name.replace(0, 1, "t")).optional()) {
          // Check if the user specifies in nanoseconds (e.g., tRCD)
          m_timing_vals(i) = JEDEC_rounding(*provided_timing, tCK_ps);
        }
      }

      // Check if there is any uninitialized timings
      for (int i = 0; i < m_timing_vals.size(); i++) {
        if (m_timing_vals(i) == -1) {
          throw ConfigurationError("In \"{}\", timing {} is not specified!", get_name(), m_timings(i));
        }
      }      

      // Set read latency
      m_read_latency = m_timing_vals("nCL") + m_timing_vals("nBL");

      // Populate the timing constraints
      #define V(timing) (m_timing_vals(timing))
      populate_timingcons(this, {
          /*** Channel ***/ 
          // CAS <-> CAS
          /// Data bus occupancy
          {.level = "channel", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA"}, .latency = V("nBL")},
          {.level = "channel", .preceding = {"WR", "WRA"}, .following = {"WR", "WRA"}, .latency = V("nBL")},

          /*** Rank ***/ 
          // CAS <-> CAS
          {.level = "rank", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA"}, .latency = V("nCCD")},
          {.level = "rank", .preceding = {"WR", "WRA"}, .following = {"WR", "WRA"}, .latency = V("nCCD")},
          /// RD <-> WR, Minimum Read to Write, Assuming tWPRE = 1 tCK                          
          {.level = "rank", .preceding = {"RD", "RDA"}, .following = {"WR", "WRA"}, .latency = V("nCL") + V("nBL") + 2 - V("nCWL")},
          /// WR <-> RD, Minimum Read after Write
          {.level = "rank", .preceding = {"WR", "WRA"}, .following = {"RD", "RDA"}, .latency = V("nCWL") + V("nBL") + V("nWTR")},
          /// CAS <-> CAS between sibling ranks, nCS (rank switching) is needed for new DQS
          {.level = "rank", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA", "WR", "WRA"}, .latency = V("nBL") + V("nCS"), .is_sibling = true},
          {.level = "rank", .preceding = {"WR", "WRA"}, .following = {"RD", "RDA"}, .latency = V("nCL")  + V("nBL") + V("nCS") - V("nCWL"), .is_sibling = true},
          /// CAS <-> PREab
          {.level = "rank", .preceding = {"RD"}, .following = {"PREA"}, .latency = V("nRTP")},
          {.level = "rank", .preceding = {"WR"}, .following = {"PREA"}, .latency = V("nCWL") + V("nBL") + V("nWR")},          
          /// RAS <-> RAS
          {.level = "rank", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRRD")},          
          {.level = "rank", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nFAW"), .window = 4},          
          {.level = "rank", .preceding = {"ACT"}, .following = {"PREA"}, .latency = V("nRAS")},          
          {.level = "rank", .preceding = {"PREA"}, .following = {"ACT"}, .latency = V("nRP")},          
          /// RAS <-> REF
          {.level = "rank", .preceding = {"ACT"}, .following = {"REFab"}, .latency = V("nRC")},          
          {.level = "rank", .preceding = {"PRE", "PREA"}, .following = {"REFab"}, .latency = V("nRP")},          
          {.level = "rank", .preceding = {"RDA"}, .following = {"REFab"}, .latency = V("nRP") + V("nRTP")},          
          {.level = "rank", .preceding = {"WRA"}, .following = {"REFab"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},          
          {.level = "rank", .preceding = {"REFab"}, .following = {"ACT"}, .latency = V("nRFC")},          

          /*** Bank ***/ 
          {.level = "bank", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRC")},  
          {.level = "bank", .preceding = {"ACT"}, .following = {"RD", "RDA", "WR", "WRA"}, .latency = V("nRCD")},  
          {.level = "bank", .preceding = {"ACT"}, .following = {"PRE"}, .latency = V("nRAS")},  
          {.level = "bank", .preceding = {"PRE"}, .following = {"ACT"}, .latency = V("nRP")},  
          {.level = "bank", .preceding = {"RD"},  .following = {"PRE"}, .latency = V("nRTP")},  
          {.level = "bank", .preceding = {"WR"},  .following = {"PRE"}, .latency = V("nCWL") + V("nBL") + V("nWR")},  
          {.level = "bank", .preceding = {"RDA"}, .following = {"ACT"}, .latency = V("nRTP") + V("nRP")},  
          {.level = "bank", .preceding = {"WRA"}, .following = {"ACT"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},  
        }
      );
      #undef V

    };

    void set_actions() {
      m_actions.resize(m_levels.size(), std::vector<ActionFunc_t<Node>>(m_commands.size()));

      // Rank Actions
      m_actions[m_levels["rank"]][m_commands["PREA"]] = Lambdas::Action::Rank::PREab<DDR3>;

      // Bank actions
      m_actions[m_levels["bank"]][m_commands["ACT"]] = Lambdas::Action::Bank::ACT<DDR3>;
      m_actions[m_levels["bank"]][m_commands["PRE"]] = Lambdas::Action::Bank::PRE<DDR3>;
      m_actions[m_levels["bank"]][m_commands["RDA"]] = Lambdas::Action::Bank::PRE<DDR3>;
      m_actions[m_levels["bank"]][m_commands["WRA"]] = Lambdas::Action::Bank::PRE<DDR3>;
    };

    void set_preqs() {
      m_preqs.resize(m_levels.size(), std::vector<PreqFunc_t<Node>>(m_commands.size()));

      // Rank Actions
      m_preqs[m_levels["rank"]][m_commands["REFab"]] = Lambdas::Preq::Rank::RequireAllBanksClosed<DDR3>;

      // Bank actions
      m_preqs[m_levels["bank"]][m_commands["RD"]] = Lambdas::Preq::Bank::RequireRowOpen<DDR3>;
      m_preqs[m_levels["bank"]][m_commands["WR"]] = Lambdas::Preq::Bank::RequireRowOpen<DDR3>;
    };

    void set_rowhits() {
      m_rowhits.resize(m_levels.size(), std::vector<RowhitFunc_t<Node>>(m_commands.size()));

      m_rowhits[m_levels["bank"]][m_commands["RD"]] = Lambdas::RowHit::Bank::RDWR<DDR3>;
      m_rowhits[m_levels["bank"]][m_commands["WR"]] = Lambdas::RowHit::Bank::RDWR<DDR3>;
    }


    void set_rowopens() {
      m_rowopens.resize(m_levels.size(), std::vector<RowhitFunc_t<Node>>(m_commands.size()));

      m_rowopens[m_levels["bank"]][m_commands["RD"]] = Lambdas::RowOpen::Bank::RDWR<DDR3>;
      m_rowopens[m_levels["bank"]][m_commands["WR"]] = Lambdas::RowOpen::Bank::RDWR<DDR3>;
    }


    void create_nodes() {
      int num_channels = m_organization.count[m_levels["channel"]];
      for (int i = 0; i < num_channels; i++) {
        Node* channel = new Node(this, nullptr, 0, i);
        m_channels.push_back(channel);
      }
    };
};


}        // namespace Ramulator
