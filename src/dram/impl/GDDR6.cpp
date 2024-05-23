#include "dram/dram.h"
#include "dram/lambdas.h"

namespace Ramulator {

class GDDR6 : public IDRAM, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IDRAM, GDDR6, "GDDR6", "GDDR6 Device Model")

  public:
    inline static const std::map<std::string, Organization> org_presets = { //Table 19 for more info
      //    name         density   DQ   Ch Bg Ba   Ro     Co
      {"GDDR6_8Gb_x8",   {8<<10,    8,  {2, 4, 4, 1<<14, 1<<11}}},
      {"GDDR6_8Gb_x16",  {8<<10,    16, {2, 4, 4, 1<<14, 1<<10}}}, 
      {"GDDR6_16Gb_x8",  {16<<10,   8,  {2, 4, 4, 1<<15, 1<<11}}},
      {"GDDR6_16Gb_x16", {16<<10,   16, {2, 4, 4, 1<<14, 1<<11}}},
      {"GDDR6_32Gb_x8",  {32<<10,   8,  {2, 4, 4, 1<<16, 1<<11}}},
      {"GDDR6_32Gb_x16", {32<<10,   16, {2, 4, 4, 1<<15, 1<<11}}},
    };

    
    inline static const std::map<std::string, std::vector<int>> timing_presets = {
      //       name                rate   nBL  nCL  nRCDRD nRCDWD  nRP   nRAS  nRC   nWR  nRTP nCWL nCCDS nCCDL nRRDS nRRDL nWTRS nWTRL nFAW  nRFC nRFCpb nRREFD nREFI  tCK_ps
      {"GDDR6_2000_1350mV_double", {2000,  8,  24,    26,     16,  26,   53,   79,   26,   4,   6,   4,    6,    7,    7,   9,    11,   28,   210,  105,   14,   3333,   570}},
      {"GDDR6_2000_1250mV_double", {2000,  8,  24,    30,     19,  30,   60,   89,   30,   4,   6,   4,    6,   11,   11,   9,    11,   42,   210,  105,   21,   3333,   570}},
      {"GDDR6_2000_1350mV_quad",   {2000,  4,  24,    26,     16,  26,   53,   79,   26,   4,   6,   4,    6,    7,    7,   9,    11,   28,   210,  105,   14,   3333,   570}},
      {"GDDR6_2000_1250mV_quad",   {2000,  4,  24,    30,     19,  30,   60,   89,   30,   4,   6,   4,    6,   11,   11,   9,    11,   42,   210,  105,   21,   3333,   570}},
    };


  /************************************************
   *                Organization
   ***********************************************/   
    const int m_internal_prefetch_size = 8;

    inline static constexpr ImplDef m_levels = {
      "channel", "bankgroup", "bank", "row", "column",    
    };


  /************************************************
   *             Requests & Commands
   ***********************************************/
    inline static constexpr ImplDef m_commands = { //figure 3
      "ACT", 
      "PREA", "PRE",
      "RD",  "WR",  "RDA",  "WRA",
      "REFab", "REFpb", "REFp2b",
    };

    inline static const ImplLUT m_command_scopes = LUT (
      m_commands, m_levels, {
        {"REFab", "channel"},  {"REFp2b",  "channel"},
        {"ACT",   "row"},
        {"PREA", "bank"},   {"PRE",  "bank"},  {"REFpb", "bank"},
        {"RD",    "column"}, {"WR",   "column"},  {"RDA",  "column"}, {"WRA",   "column"},
      }
    );

    inline static const ImplLUT m_command_meta = LUT<DRAMCommandMeta> (
      m_commands, {
                // open?   close?   access?  refresh?
        {"ACT",   {true,   false,   false,   false}},
        {"PREA",  {false,  true,    false,   false}},
        {"PRE",   {false,  true,    false,   false}},
        {"RD",    {false,  false,   true,    false}},
        {"WR",    {false,  false,   true,    false}},
        {"RDA",   {false,  true,    true,    false}},
        {"WRA",   {false,  true,    true,    false}},
        {"REFab", {false,  false,   false,   true }}, //double check
        {"REFpb", {false,  false,   false,   true }},
        {"REFp2b",{false,  false,   false,   true }},
      }
    );

    inline static constexpr ImplDef m_requests = {
      "read", "write", "all-bank-refresh", "PREsb",
    };

    inline static const ImplLUT m_request_translations = LUT (
      m_requests, m_commands, {
        {"read", "RD"}, {"write", "WR"}, {"all-bank-refresh", "REFab"}, {"PREsb", "PRE"}
      }
    );

   
  /************************************************
   *                   Timing
   ***********************************************/
  //delete nCS
    inline static constexpr ImplDef m_timings = {
      "rate", 
      "nBL", "nCL", "nRCDRD", "nRCDWD", "nRP", "nRAS", "nRC", "nWR", "nRTP", "nCWL",
      "nCCDS", "nCCDL",
      "nRRDS", "nRRDL",
      "nWTRS", "nWTRL",
      "nFAW",
      "nRFC","nREFI", "nRREFD", "nRFCpb",
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
        {"bankgroup", "N/A"},
        {"bank",      "Closed"},
        {"row",       "Closed"},
        {"column",    "N/A"},
      }
    );

  public:
    struct Node : public DRAMNodeBase<GDDR6> {
      Node(GDDR6* dram, Node* parent, int level, int id) : DRAMNodeBase<GDDR6>(dram, parent, level, id) {};
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
      size_t _density = size_t(m_organization.count[m_levels["channel"]]) *
                        size_t(m_organization.count[m_levels["bankgroup"]]) *
                        size_t(m_organization.count[m_levels["bank"]]) *
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
          case 8:  return 0;
          case 16: return 1;
          default: return -1;
        }
      }(m_organization.dq);

      int rate_id = [](int rate) -> int { //should low voltage operation be added here?
        switch (rate) {
          case 2000:  return 0;
          default:    return -1;
        }
      }(m_timing_vals("rate"));

      // Tables for secondary timings determined by the frequency, density, and DQ width.
      // Defined in the JEDEC standard (e.g., Table 169-170, JESD79-4C).

      //update these values
      constexpr int nRRDS_TABLE[2][1] = {
      // 2000
        { 4 },   // x8
        { 5 },   // x16
      };
      constexpr int nRRDL_TABLE[2][1] = {
      // 2000
        { 5 },  // x8
        { 6 },  // x16
      };
      constexpr int nFAW_TABLE[2][1] = {
      // 2000
        { 20 },  // x8
        { 28 },  // x16
      };

      if (dq_id != -1 && rate_id != -1) {
        m_timing_vals("nRRDS") = nRRDS_TABLE[dq_id][rate_id];
        m_timing_vals("nRRDL") = nRRDL_TABLE[dq_id][rate_id];
        m_timing_vals("nFAW")  = nFAW_TABLE [dq_id][rate_id];
      }

      // Refresh timings
      // tRFC table (unit is nanosecond!)
      constexpr int tRFC_TABLE[3][3] = {
      //  4Gb   8Gb  16Gb
        { 260,  360,  550}, // Normal refresh (tRFC1)
        { 160,  260,  350}, // FGR 2x (tRFC2)
        { 110,  160,  260}, // FGR 4x (tRFC4)
      };

      // tREFI(base) table (unit is nanosecond!)
      constexpr int tREFI_BASE = 7800;
      int density_id = [](int density_Mb) -> int { 
        switch (density_Mb) {
          case 4096:  return 0;
          case 8192:  return 1;
          case 16384: return 2;
          default:    return -1;
        }
      }(m_organization.density);

      m_timing_vals("nRFC")  = JEDEC_rounding(tRFC_TABLE[0][density_id], tCK_ps);
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

          /*** Rank (or different BankGroup) ***/
          // changed from rank to channel, some duplicates, what takes
          // CAS <-> CAS
          /// nCCDS is the minimal latency for column commands 
          {.level = "channel", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA"}, .latency = V("nCCDS")},
          {.level = "channel", .preceding = {"WR", "WRA"}, .following = {"WR", "WRA"}, .latency = V("nCCDS")},
          /// RD <-> WR, Minimum Read to Write, Assuming tWPRE = 1 tCK                          
          {.level = "channel", .preceding = {"RD", "RDA"}, .following = {"WR", "WRA"}, .latency = V("nCL") + V("nBL") + 3 - V("nCWL") + 1}, //+ 1 is assuming bus turn around time
          /// WR <-> RD, Minimum Read after Write
          {.level = "channel", .preceding = {"WR", "WRA"}, .following = {"RD", "RDA"}, .latency = V("nCWL") + V("nBL") + V("nWTRS")},
          /// CAS <-> PREA
          {.level = "channel", .preceding = {"RD"}, .following = {"PREA"}, .latency = V("nRTP")},
          {.level = "channel", .preceding = {"WR"}, .following = {"PREA"}, .latency = V("nCWL") + V("nBL") + V("nWR")},          
          /// RAS <-> RAS
          {.level = "channel", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRRDS")},          
          {.level = "channel", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nFAW"), .window = 4},       
          {.level = "channel", .preceding = {"ACT"}, .following = {"PRE"}, .latency = V("nRAS")},          
          {.level = "channel", .preceding = {"PRE"}, .following = {"ACT"}, .latency = V("nRP")},          
          /// RAS <-> REF
          {.level = "channel", .preceding = {"ACT"}, .following = {"REFab"}, .latency = V("nRC")},          
          {.level = "channel", .preceding = {"PRE"}, .following = {"REFab"}, .latency = V("nRP")},          
          {.level = "channel", .preceding = {"RDA"}, .following = {"REFab"}, .latency = V("nRP") + V("nRTP")},          
          {.level = "channel", .preceding = {"WRA"}, .following = {"REFab"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},          
          {.level = "channel", .preceding = {"REFab"}, .following = {"ACT"}, .latency = V("nRFC")},          
          
          /// RAS <-> REFp2b
          {.level = "channel", .preceding = {"ACT"}, .following = {"REFp2b"}, .latency = V("nRRDL")},          
          {.level = "channel", .preceding = {"PRE"}, .following = {"REFp2b"}, .latency = V("nRP")},          
          {.level = "channel", .preceding = {"RDA"}, .following = {"REFp2b"}, .latency = V("nRP") + V("nRTP")},          
          {.level = "channel", .preceding = {"WRA"}, .following = {"REFp2b"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},          
          {.level = "channel", .preceding = {"REFp2b"}, .following = {"ACT"}, .latency = V("nRREFD")},   

          /// RAS <-> REFpb
          {.level = "channel", .preceding = {"ACT"}, .following = {"REFpb"}, .latency = V("nRRDL")},          
          {.level = "channel", .preceding = {"PRE"}, .following = {"REFpb"}, .latency = V("nRP")},          
          {.level = "channel", .preceding = {"RDA"}, .following = {"REFpb"}, .latency = V("nRP") + V("nRTP")},          
          {.level = "channel", .preceding = {"WRA"}, .following = {"REFpb"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},          
          {.level = "channel", .preceding = {"REFpb"}, .following = {"ACT"}, .latency = V("nRREFD")},   


          /*** Same Bank Group ***/ 
          /// CAS <-> CAS
          {.level = "bankgroup", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA"}, .latency = V("nCCDL")},          
          {.level = "bankgroup", .preceding = {"WR", "WRA"}, .following = {"WR", "WRA"}, .latency = V("nCCDL")},          
          {.level = "bankgroup", .preceding = {"WR", "WRA"}, .following = {"RD", "RDA"}, .latency = V("nCWL") + V("nBL") + V("nWTRL")},
          /// RAS <-> RAS
          {.level = "bankgroup", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRRDL")},  

          /*** Bank ***/ 
          /// CAS <-> RAS
          {.level = "bank", .preceding = {"ACT"}, .following = {"RD", "RDA"}, .latency = V("nRCDRD")}, 
          {.level = "bank", .preceding = {"ACT"}, .following = {"WR", "WRA"}, .latency = V("nRCDWD")},
          {.level = "bank", .preceding = {"ACT"}, .following = {"PRE"}, .latency = V("nRAS")},  
          {.level = "bank", .preceding = {"PRE"}, .following = {"ACT"}, .latency = V("nRP")},  
          {.level = "bank", .preceding = {"RD"},  .following = {"PRE"}, .latency = V("nRTP")},  
          {.level = "bank", .preceding = {"WR"},  .following = {"PRE"}, .latency = V("nCWL") + V("nBL") + V("nWR")},  
          {.level = "bank", .preceding = {"RDA"}, .following = {"ACT"}, .latency = V("nRTP") + V("nRP")},  
          {.level = "bank", .preceding = {"WRA"}, .following = {"ACT"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},  

          /// RAS <-> REFpb
          {.level = "bank", .preceding = {"ACT"}, .following = {"REFpb"}, .latency = V("nRC")},          
          {.level = "bank", .preceding = {"PRE"}, .following = {"REFpb"}, .latency = V("nRP")},          
          {.level = "bank", .preceding = {"RDA"}, .following = {"REFpb"}, .latency = V("nRP") + V("nRTP")},          
          {.level = "bank", .preceding = {"WRA"}, .following = {"REFpb"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},          
          {.level = "bank", .preceding = {"REFpb"}, .following = {"ACT"}, .latency = V("nRFCpb")},   

          /// RAS <-> RAS
          //{.level = "bank", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRC")}, //should this be added?
        }
      );
      #undef V

    };

    void set_actions() {
      m_actions.resize(m_levels.size(), std::vector<ActionFunc_t<Node>>(m_commands.size()));

      // Channel Actions 
      m_actions[m_levels["channel"]][m_commands["PREA"]] = Lambdas::Action::Channel::PREab<GDDR6>; 

      // Bank actions
      m_actions[m_levels["bank"]][m_commands["ACT"]] = Lambdas::Action::Bank::ACT<GDDR6>;
      m_actions[m_levels["bank"]][m_commands["PRE"]] = Lambdas::Action::Bank::PRE<GDDR6>;
      m_actions[m_levels["bank"]][m_commands["RDA"]] = Lambdas::Action::Bank::PRE<GDDR6>;
      m_actions[m_levels["bank"]][m_commands["WRA"]] = Lambdas::Action::Bank::PRE<GDDR6>;
    };

    void set_preqs() {
      m_preqs.resize(m_levels.size(), std::vector<PreqFunc_t<Node>>(m_commands.size()));

      // Channel Actions 
      m_preqs[m_levels["channel"]][m_commands["REFab"]] = Lambdas::Preq::Channel::RequireAllBanksClosed<GDDR6>; 

      // Bank actions
      m_preqs[m_levels["bank"]][m_commands["RD"]] = Lambdas::Preq::Bank::RequireRowOpen<GDDR6>;
      m_preqs[m_levels["bank"]][m_commands["WR"]] = Lambdas::Preq::Bank::RequireRowOpen<GDDR6>;
      //m_preqs[m_levels["channel"]][m_commands["REFpb"]] = Lambdas::Preq::Bank::RequireAllBanksClosed<GDDR6>; // can RequireSameBanksClosed be used, or is RequireBankClosed needed?
      //m_preqs[m_levels["channel"]][m_commands["REFp2b"]] = Lambdas::Preq::Bank::RequireAllBanksClosed<GDDR6>; 
    };

    void set_rowhits() {
      m_rowhits.resize(m_levels.size(), std::vector<RowhitFunc_t<Node>>(m_commands.size()));

      m_rowhits[m_levels["bank"]][m_commands["RD"]] = Lambdas::RowHit::Bank::RDWR<GDDR6>;
      m_rowhits[m_levels["bank"]][m_commands["WR"]] = Lambdas::RowHit::Bank::RDWR<GDDR6>;
    }


    void set_rowopens() {
      m_rowopens.resize(m_levels.size(), std::vector<RowhitFunc_t<Node>>(m_commands.size()));

      m_rowopens[m_levels["bank"]][m_commands["RD"]] = Lambdas::RowOpen::Bank::RDWR<GDDR6>;
      m_rowopens[m_levels["bank"]][m_commands["WR"]] = Lambdas::RowOpen::Bank::RDWR<GDDR6>;
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

