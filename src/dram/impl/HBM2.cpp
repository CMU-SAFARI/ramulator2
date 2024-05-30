#include "dram/dram.h"
#include "dram/lambdas.h"

namespace Ramulator {

class HBM2 : public IDRAM, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IDRAM, HBM2, "HBM2", "HBM2 Device Model")

  public:
    inline static const std::map<std::string, Organization> org_presets = {
      //   name     density   DQ    Ch Pch  Bg Ba   Ro     Co
      {"HBM2_2Gb",   {2<<10,  128,  {1, 2,  4,  2, 1<<14, 1<<6}}},
      {"HBM2_4Gb",   {4<<10,  128,  {1, 2,  4,  4, 1<<14, 1<<6}}},
      {"HBM2_8Gb",   {6<<10,  128,  {1, 2,  4,  4, 1<<15, 1<<6}}},
    };

    inline static const std::map<std::string, std::vector<int>> timing_presets = {
      //   name       rate   nBL  nCL  nRCDRD  nRCDWR  nRP  nRAS  nRC  nWR  nRTPS  nRTPL  nCWL  nCCDS  nCCDL  nRRDS  nRRDL  nWTRS  nWTRL  nRTW  nFAW  nRFC  nRFCSB  nREFI  nREFISB  nRREFD  tCK_ps
      {"HBM2_2Gbps",  {2000,   4,   7,    7,      7,     7,   17,  19,   8,    2,     3,    2,    1,      2,     2,     3,     3,     4,    3,    15,   -1,   160,   3900,     -1,      8,   1000}},
      // TODO: Find more sources on HBM2 timings...
    };


  /************************************************
   *                Organization
   ***********************************************/   
    const int m_internal_prefetch_size = 2;

    inline static constexpr ImplDef m_levels = {
      "channel", "pseudochannel", "bankgroup", "bank", "row", "column",    
    };


  /************************************************
   *             Requests & Commands
   ***********************************************/
    inline static constexpr ImplDef m_commands = {
      "ACT", 
      "PRE", "PREA",
      "RD",  "WR",  "RDA",  "WRA",
      "REFab", "REFsb"
    };

    inline static const ImplLUT m_command_scopes = LUT (
      m_commands, m_levels, {
        {"ACT",   "row"},
        {"PRE",   "bank"},    {"PREA",   "channel"},
        {"RD",    "column"},  {"WR",     "column"}, {"RDA",   "column"}, {"WRA",   "column"},
        {"REFab", "channel"}, {"REFsb",  "bank"},
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
        {"REFsb", {false,  false,   false,   true }},
      }
    );

    inline static constexpr ImplDef m_requests = {
      "read", "write", "all-bank-refresh", "per-bank-refresh"
    };

    inline static const ImplLUT m_request_translations = LUT (
      m_requests, m_commands, {
        {"read", "RD"}, {"write", "WR"}, {"all-bank-refresh", "REFab"}, {"per-bank-refresh", "REFsb"},
      }
    );

   
  /************************************************
   *                   Timing
   ***********************************************/
    inline static constexpr ImplDef m_timings = {
      "rate", 
      "nBL", "nCL", "nRCDRD", "nRCDWR", "nRP", "nRAS", "nRC", "nWR", "nRTPS", "nRTPL", "nCWL",
      "nCCDS", "nCCDL",
      "nRRDS", "nRRDL",
      "nWTRS", "nWTRL",
      "nRTW",
      "nFAW",
      "nRFC", "nRFCSB", "nREFI", "nREFISB", "nRREFD",
      "tCK_ps"
    };


  /************************************************
   *                 Node States
   ***********************************************/
    inline static constexpr ImplDef m_states = {
       "Opened", "Closed", "N/A", "Refreshing"
    };

    inline static const ImplLUT m_init_states = LUT (
      m_levels, m_states, {
        {"channel",       "N/A"}, 
        {"pseudochannel", "N/A"}, 
        {"bankgroup",     "N/A"},
        {"bank",          "Closed"},
        {"row",           "Closed"},
        {"column",        "N/A"},
      }
    );

  public:
    struct Node : public DRAMNodeBase<HBM2> {
      Node(HBM2* dram, Node* parent, int level, int id) : DRAMNodeBase<HBM2>(dram, parent, level, id) {};
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

      // Sanity check: is the calculated channel density the same as the provided one?
      size_t _density = size_t(m_organization.count[m_levels["pseudochannel"]]) *
                        size_t(m_organization.count[m_levels["bankgroup"]]) *
                        size_t(m_organization.count[m_levels["bank"]]) *
                        size_t(m_organization.count[m_levels["row"]]) *
                        size_t(m_organization.count[m_levels["column"]]) *
                        size_t(m_organization.dq);
      _density >>= 20;
      if (m_organization.density != _density) {
        throw ConfigurationError(
            "Calculated {} channel density {} Mb does not equal the provided density {} Mb!", 
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

      // Refresh timings
      // tRFC table (unit is nanosecond!)
      constexpr int tRFC_TABLE[1][4] = {
      //  2Gb   4Gb   8Gb  16Gb
        { 160,  260,  350,  450},
      };

      // tRFC table (unit is nanosecond!)
      constexpr int tREFISB_TABLE[1][4] = {
      //  2Gb    4Gb    8Gb    16Gb
        { 4875,  4875,  2438,  2438},
      };

      int density_id = [](int density_Mb) -> int { 
        switch (density_Mb) {
          case 2048:  return 0;
          case 4096:  return 1;
          case 8192:  return 2;
          case 16384: return 3;
          default:    return -1;
        }
      }(m_organization.density);

      m_timing_vals("nRFC")  = JEDEC_rounding(tRFC_TABLE[0][density_id], tCK_ps);
      m_timing_vals("nREFISB")  = JEDEC_rounding(tRFC_TABLE[0][density_id], tCK_ps);

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
          /// 2-cycle ACT command (for row commands)
          {.level = "channel", .preceding = {"ACT"}, .following = {"ACT", "PRE", "PREA", "REFab", "REFsb"}, .latency = 2},

          /*** Pseudo Channel (Table 3 — Array Access Timings Counted Individually Per Pseudo Channel, JESD-235C) ***/ 
          // RAS <-> RAS
          {.level = "pseudochannel", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRRDS")},
          /// 4-activation window restriction
          {.level = "pseudochannel", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nFAW"), .window = 4},

          /// ACT actually happens on the 2-nd cycle of ACT, so +1 cycle to nRRD
          {.level = "pseudochannel", .preceding = {"ACT"}, .following = {"REFsb"}, .latency = V("nRRDS") + 1},
          /// nRREFD is the latency between REFsb <-> REFsb to *different* banks
          {.level = "pseudochannel", .preceding = {"REFsb"}, .following = {"REFsb"}, .latency = V("nRREFD")},
          /// nRREFD is the latency between REFsb <-> ACT to *different* banks. -1 as ACT happens on its 2nd cycle
          {.level = "pseudochannel", .preceding = {"REFsb"}, .following = {"ACT"}, .latency = V("nRREFD") - 1},

          // CAS <-> CAS
          /// Data bus occupancy
          {.level = "pseudochannel", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA"}, .latency = V("nBL")},
          {.level = "pseudochannel", .preceding = {"WR", "WRA"}, .following = {"WR", "WRA"}, .latency = V("nBL")},

          // CAS <-> CAS
          /// nCCDS is the minimal latency for column commands 
          {.level = "pseudochannel", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA"}, .latency = V("nCCDS")},
          {.level = "pseudochannel", .preceding = {"WR", "WRA"}, .following = {"WR", "WRA"}, .latency = V("nCCDS")},
          /// RD <-> WR, Minimum Read to Write, Assuming tWPRE = 1 tCK                          
          {.level = "pseudochannel", .preceding = {"RD", "RDA"}, .following = {"WR", "WRA"}, .latency = V("nCL") + V("nBL") + 2 - V("nCWL")},
          /// WR <-> RD, Minimum Read after Write
          {.level = "pseudochannel", .preceding = {"WR", "WRA"}, .following = {"RD", "RDA"}, .latency = V("nCWL") + V("nBL") + V("nWTRS")},
          /// CAS <-> PREab
          {.level = "pseudochannel", .preceding = {"RD"}, .following = {"PREA"}, .latency = V("nRTPS")},
          {.level = "pseudochannel", .preceding = {"WR"}, .following = {"PREA"}, .latency = V("nCWL") + V("nBL") + V("nWR")},          
          /// RAS <-> RAS
          {.level = "pseudochannel", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRRDS")},          
          {.level = "pseudochannel", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nFAW"), .window = 4},          
          {.level = "pseudochannel", .preceding = {"ACT"}, .following = {"PREA"}, .latency = V("nRAS")},          
          {.level = "pseudochannel", .preceding = {"PREA"}, .following = {"ACT"}, .latency = V("nRP")},          
          /// RAS <-> REF
          {.level = "pseudochannel", .preceding = {"ACT"}, .following = {"REFab"}, .latency = V("nRC")},          
          {.level = "pseudochannel", .preceding = {"PRE", "PREA"}, .following = {"REFab"}, .latency = V("nRP")},          
          {.level = "pseudochannel", .preceding = {"RDA"}, .following = {"REFab"}, .latency = V("nRP") + V("nRTPS")},          
          {.level = "pseudochannel", .preceding = {"WRA"}, .following = {"REFab"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},          
          {.level = "pseudochannel", .preceding = {"REFab"}, .following = {"ACT", "REFsb"}, .latency = V("nRFC")},          

          /*** Same Bank Group ***/ 
          /// CAS <-> CAS
          {.level = "bankgroup", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA"}, .latency = V("nCCDL")},          
          {.level = "bankgroup", .preceding = {"WR", "WRA"}, .following = {"WR", "WRA"}, .latency = V("nCCDL")},          
          {.level = "bankgroup", .preceding = {"WR", "WRA"}, .following = {"RD", "RDA"}, .latency = V("nCWL") + V("nBL") + V("nWTRL")},
          /// RAS <-> RAS
          {.level = "bankgroup", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRRDL")},  
          {.level = "bankgroup", .preceding = {"ACT"}, .following = {"REFsb"}, .latency = V("nRRDL") + 1},  
          {.level = "bankgroup", .preceding = {"REFsb"}, .following = {"ACT"}, .latency = V("nRRDL") - 1},  

          {.level = "bank", .preceding = {"RD"},  .following = {"PRE"}, .latency = V("nRTPS")},  


          /*** Bank ***/ 
          {.level = "bank", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRC")},  
          {.level = "bank", .preceding = {"ACT"}, .following = {"RD", "RDA"}, .latency = V("nRCDRD")},  
          {.level = "bank", .preceding = {"ACT"}, .following = {"WR", "WRA"}, .latency = V("nRCDWR")},  
          {.level = "bank", .preceding = {"ACT"}, .following = {"PRE"}, .latency = V("nRAS")},  
          {.level = "bank", .preceding = {"PRE"}, .following = {"ACT"}, .latency = V("nRP")},  
          {.level = "bank", .preceding = {"RD"},  .following = {"PRE"}, .latency = V("nRTPL")},  
          {.level = "bank", .preceding = {"WR"},  .following = {"PRE"}, .latency = V("nCWL") + V("nBL") + V("nWR")},  
          {.level = "bank", .preceding = {"RDA"}, .following = {"ACT", "REFsb"}, .latency = V("nRTPL") + V("nRP")},  
          {.level = "bank", .preceding = {"WRA"}, .following = {"ACT", "REFsb"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},  
        }
      );
      #undef V

    };

    void set_actions() {
      m_actions.resize(m_levels.size(), std::vector<ActionFunc_t<Node>>(m_commands.size()));

      // Channel Actions
      m_actions[m_levels["channel"]][m_commands["PREA"]] = Lambdas::Action::Channel::PREab<HBM2>;

      // Bank actions
      m_actions[m_levels["bank"]][m_commands["ACT"]] = Lambdas::Action::Bank::ACT<HBM2>;
      m_actions[m_levels["bank"]][m_commands["PRE"]] = Lambdas::Action::Bank::PRE<HBM2>;
      m_actions[m_levels["bank"]][m_commands["RDA"]] = Lambdas::Action::Bank::PRE<HBM2>;
      m_actions[m_levels["bank"]][m_commands["WRA"]] = Lambdas::Action::Bank::PRE<HBM2>;
    };

    void set_preqs() {
      m_preqs.resize(m_levels.size(), std::vector<PreqFunc_t<Node>>(m_commands.size()));

      // Channel Actions
      m_preqs[m_levels["channel"]][m_commands["REFab"]] = Lambdas::Preq::Channel::RequireAllBanksClosed<HBM2>;

      // Bank actions
      m_preqs[m_levels["bank"]][m_commands["REFsb"]] = Lambdas::Preq::Bank::RequireBankClosed<HBM2>;
      m_preqs[m_levels["bank"]][m_commands["RD"]] = Lambdas::Preq::Bank::RequireRowOpen<HBM2>;
      m_preqs[m_levels["bank"]][m_commands["WR"]] = Lambdas::Preq::Bank::RequireRowOpen<HBM2>;
    };

    void set_rowhits() {
      m_rowhits.resize(m_levels.size(), std::vector<RowhitFunc_t<Node>>(m_commands.size()));

      m_rowhits[m_levels["bank"]][m_commands["RD"]] = Lambdas::RowHit::Bank::RDWR<HBM2>;
      m_rowhits[m_levels["bank"]][m_commands["WR"]] = Lambdas::RowHit::Bank::RDWR<HBM2>;
    }


    void set_rowopens() {
      m_rowopens.resize(m_levels.size(), std::vector<RowhitFunc_t<Node>>(m_commands.size()));

      m_rowopens[m_levels["bank"]][m_commands["RD"]] = Lambdas::RowOpen::Bank::RDWR<HBM2>;
      m_rowopens[m_levels["bank"]][m_commands["WR"]] = Lambdas::RowOpen::Bank::RDWR<HBM2>;
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