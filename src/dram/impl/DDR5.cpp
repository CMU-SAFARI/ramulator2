#include "dram/dram.h"
#include "dram/lambdas.h"

namespace Ramulator {

class DDR5 : public IDRAM, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IDRAM, DDR5, "DDR5", "DDR5 Device Model")

  public:
    inline static const std::map<std::string, Organization> org_presets = {
      //   name         density   DQ   Ch Ra Bg Ba   Ro     Co
      {"DDR5_8Gb_x4",   {8<<10,   4,  {1, 1, 8, 2, 1<<16, 1<<11}}},
      {"DDR5_8Gb_x8",   {8<<10,   8,  {1, 1, 8, 2, 1<<16, 1<<10}}},
      {"DDR5_8Gb_x16",  {8<<10,   16, {1, 1, 4, 2, 1<<16, 1<<10}}},
      {"DDR5_16Gb_x4",  {16<<10,  4,  {1, 1, 8, 4, 1<<16, 1<<11}}},
      {"DDR5_16Gb_x8",  {16<<10,  8,  {1, 1, 8, 4, 1<<16, 1<<10}}},
      {"DDR5_16Gb_x16", {16<<10,  16, {1, 1, 4, 4, 1<<16, 1<<10}}},
      {"DDR5_32Gb_x4",  {32<<10,  4,  {1, 1, 8, 4, 1<<17, 1<<11}}},
      {"DDR5_32Gb_x8",  {32<<10,  8,  {1, 1, 8, 4, 1<<17, 1<<10}}},
      {"DDR5_32Gb_x16", {32<<10,  16, {1, 1, 4, 4, 1<<17, 1<<10}}},
      // {"DDR5_64Gb_x4",  {64<<10,  4,  {1, 1, 8, 4, 1<<18, 1<<11}}},
      // {"DDR5_64Gb_x8",  {64<<10,  8,  {1, 1, 8, 4, 1<<18, 1<<10}}},
      // {"DDR5_64Gb_x16", {64<<10,  16, {1, 1, 4, 4, 1<<18, 1<<10}}},
    };

    inline static const std::map<std::string, std::vector<int>> timing_presets = {
      //   name         rate   nBL  nCL nRCD   nRP  nRAS   nRC   nWR  nRTP nCWL nPPD nCCDS nCCDS_WR nCCDS_WTR nCCDL nCCDL_WR nCCDL_WTR nRRDS nRRDL nFAW nRFC1 nRFC2 nRFCsb nREFI nREFSBRD nRFM1 nRFM2 nRFMsb nDRFMab nDRFMsb nCS, tCK_ps
      {"DDR5_3200AN",  {3200,   8,  24,  24,   24,   52,   75,   48,   12,  22,  2,    8,     8,     22+8+4,    8,     16,    22+8+16,   8,   -1,   -1,  -1,   -1,   -1,    -1,     30,    -1,   -1,   -1,     -1,     -1,    2,   625}},
      {"DDR5_3200BN",  {3200,   8,  26,  26,   26,   52,   77,   48,   12,  24,  2,    8,     8,     24+8+4,    8,     16,    24+8+16,   8,   -1,   -1,  -1,   -1,   -1,    -1,     30,    -1,   -1,   -1,     -1,     -1,    2,   625}},
      {"DDR5_3200C",   {3200,   8,  28,  28,   28,   52,   79,   48,   12,  26,  2,    8,     8,     26+8+4,    8,     16,    26+8+16,   8,   -1,   -1,  -1,   -1,   -1,    -1,     30,    -1,   -1,   -1,     -1,     -1,    2,   625}},
    };

  /************************************************
   *                Organization
   ***********************************************/   
    const int m_internal_prefetch_size = 16;

    inline static constexpr ImplDef m_levels = {
      "channel", "rank", "bankgroup", "bank", "row", "column",    
    };


  /************************************************
   *             Requests & Commands
   ***********************************************/
    inline static constexpr ImplDef m_commands = {
      "ACT", 
      "PRE", "PREA", "PREsb",
      "RD",  "WR",  "RDA",  "WRA",
      "REFab",  "REFsb",
      "RFMab",  "RFMsb",
      "DRFMab", "DRFMsb",
    };

    inline static const ImplLUT m_command_scopes = LUT (
      m_commands, m_levels, {
        {"ACT",   "row"},
        {"PRE",   "bank"},   {"PREA",   "rank"},   {"PREsb", "bankgroup"},
        {"RD",    "column"}, {"WR",     "column"}, {"RDA",   "column"}, {"WRA",   "column"},
        {"REFab",  "rank"},  {"REFsb",  "bankgroup"},  
        {"RFMab",  "rank"},  {"RFMsb",  "bankgroup"},  
        {"DRFMab", "rank"},  {"DRFMsb", "bankgroup"},  
      }
    );

    inline static const ImplLUT m_command_meta = LUT<DRAMCommandMeta> (
      m_commands, {
                // open?   close?   access?  refresh?
        {"ACT",   {true,   false,   false,   false}},
        {"PRE",   {false,  true,    false,   false}},
        {"PREA",  {false,  true,    false,   false}},
        {"PREsb", {false,  true,    false,   false}},
        {"RD",    {false,  false,   true,    false}},
        {"WR",    {false,  false,   true,    false}},
        {"RDA",   {false,  true,    true,    false}},
        {"WRA",   {false,  true,    true,    false}},
        {"REFab", {false,  false,   false,   true }},
      }
    );

    inline static constexpr ImplDef m_requests = {
      "read", "write", 
      "all-bank-refresh", "same-bank-refresh", 
      "rfm", "same-bank-rfm",
      "directed-rfm", "same-bank-directed-rfm",
    };

    inline static const ImplLUT m_request_translations = LUT (
      m_requests, m_commands, {
        {"read", "RD"}, {"write", "WR"}, 
        {"all-bank-refresh", "REFab"}, {"same-bank-refresh", "REFsb"}, 
        {"rfm", "RFMab"}, {"same-bank-rfm", "RFMsb"}, 
        {"directed-rfm", "DRFMab"}, {"same-bank-directed-rfm", "DRFMsb"}, 
      }
    );

   
  /************************************************
   *                   Timing
   ***********************************************/
    inline static constexpr ImplDef m_timings = {
      "rate", 
      "nBL", "nCL", "nRCD", "nRP", "nRAS", "nRC", "nWR", "nRTP", "nCWL",
      "nPPD",
      "nCCDS", "nCCDS_WR", "nCCDS_WTR", 
      "nCCDL", "nCCDL_WR", "nCCDL_WTR", 
      "nRRDS", "nRRDL",
      "nFAW",
      "nRFC1", "nRFC2", "nRFCsb", "nREFI", "nREFSBRD",
      "nRFM1", "nRFM2", "nRFMsb", 
      "nDRFMab", "nDRFMsb", 
      "nCS",
      "tCK_ps"
    };


  /************************************************
   *                 Node States
   ***********************************************/
    inline static constexpr ImplDef m_states = {
       "Opened", "Closed", "PowerUp", "N/A"
    };

    inline static const ImplLUT m_init_states = LUT (
      m_levels, m_states, {
        {"channel",   "N/A"}, 
        {"rank",      "PowerUp"},
        {"bankgroup", "N/A"},
        {"bank",      "Closed"},
        {"row",       "Closed"},
        {"column",    "N/A"},
      }
    );

  public:
    struct Node : public DRAMNodeBase<DDR5> {
      Node(DDR5* dram, Node* parent, int level, int id) : DRAMNodeBase<DDR5>(dram, parent, level, id) {};
    };
    std::vector<Node*> m_channels;
    
    FuncMatrix<ActionFunc_t<Node>>  m_actions;
    FuncMatrix<PreqFunc_t<Node>>    m_preqs;
    FuncMatrix<RowhitFunc_t<Node>>  m_rowhits;
    FuncMatrix<RowopenFunc_t<Node>> m_rowopens;


  /************************************************
   *                 RFM Related
   ***********************************************/
  public:
    int m_BRC = 2;


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

  private:
    void set_organization() {
      // Channel width
      m_channel_width = param_group("org").param<int>("channel_width").default_val(32);

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
      size_t _density = size_t(m_organization.count[m_levels["bankgroup"]]) *
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
          case 4:  return 0;
          case 8:  return 1;
          case 16: return 2;
          default: return -1;
        }
      }(m_organization.dq);

      int rate_id = [](int rate) -> int {
        switch (rate) {
          case 3200:  return 0;
          default:    return -1;
        }
      }(m_timing_vals("rate"));

      constexpr int nRRDL_TABLE[3][1] = {
      // 3200  
        { 5, },  // x4
        { 5, },  // x8
        { 5, },  // x16
      };
      constexpr int nFAW_TABLE[3][1] = {
      // 3200  
        { 40, },  // x4
        { 32, },  // x8
        { 32, },  // x16
      };

      if (dq_id != -1 && rate_id != -1) {
        m_timing_vals("nRRDL") = nRRDL_TABLE[dq_id][rate_id];
        m_timing_vals("nFAW")  = nFAW_TABLE [dq_id][rate_id];
      }

      // tCCD_L_WR2 (with RMW) table
      constexpr int nCCD_L_WR2_TABLE[1] = {
      // 3200  
        32,
      };
      if (dq_id == 0) {
        m_timing_vals("nCCDL_WR") = nCCD_L_WR2_TABLE[rate_id];
      }

      // Refresh timings
      // tRFC table (unit is nanosecond!)
      constexpr int tRFC_TABLE[2][3] = {
      //  8Gb   16Gb  32Gb  
        { 195,  295,  410 }, // Normal refresh (tRFC1)
        { 130,  160,  220 }, // FGR 2x (tRFC2)
      };

      // tRFCsb table (unit is nanosecond!)
      constexpr int tRFCsb_TABLE[1][3] = {
      //  8Gb   16Gb  32Gb  
        { 115,  130,  190 }, // Normal refresh (tRFC1)
      };

      // tREFI(base) table (unit is nanosecond!)
      constexpr int tREFI_BASE = 3900;
      int density_id = [](int density_Mb) -> int { 
        switch (density_Mb) {
          case 8192:  return 0;
          case 16384: return 1;
          case 32768: return 2;
          default:    return -1;
        }
      }(m_organization.density);

      m_timing_vals("nRFC1")  = JEDEC_rounding_DDR5(tRFC_TABLE[0][density_id], tCK_ps);
      m_timing_vals("nRFC2")  = JEDEC_rounding_DDR5(tRFC_TABLE[1][density_id], tCK_ps);
      m_timing_vals("nRFCsb") = JEDEC_rounding_DDR5(tRFCsb_TABLE[0][density_id], tCK_ps);
      m_timing_vals("nREFI")  = JEDEC_rounding_DDR5(tREFI_BASE, tCK_ps);

      m_timing_vals("nRFM1")  = m_timing_vals("nRFC1");
      m_timing_vals("nRFM2")  = m_timing_vals("nRFC2");
      m_timing_vals("nRFMsb") = m_timing_vals("nRFCsb");

      // tRRF table (unit is nanosecond!)
      constexpr int tRRFsb_TABLE[2][3] = {
      //  8Gb 16Gb 32Gb  
        { 70,  70,  70 }, // tRRFab
        { 60,  60,  60 }, // tRRFsb
      };
      m_BRC = param_group("RFM").param<int>("BRC").default_val(2);
      m_timing_vals("nDRFMab") = m_BRC * JEDEC_rounding_DDR5(tRRFsb_TABLE[0][density_id], tCK_ps);
      m_timing_vals("nDRFMsb") = m_BRC * JEDEC_rounding_DDR5(tRRFsb_TABLE[1][density_id], tCK_ps);


      // Overwrite timing parameters with any user-provided value
      // Rate and tCK should not be overwritten
      for (int i = 1; i < m_timings.size() - 1; i++) {
        auto timing_name = std::string(m_timings(i));

        if (auto provided_timing = param_group("timing").param<int>(timing_name).optional()) {
          // Check if the user specifies in the number of cycles (e.g., nRCD)
          m_timing_vals(i) = *provided_timing;
        } else if (auto provided_timing = param_group("timing").param<float>(timing_name.replace(0, 1, "t")).optional()) {
          // Check if the user specifies in nanoseconds (e.g., tRCD)
          m_timing_vals(i) = JEDEC_rounding_DDR5(*provided_timing, tCK_ps);
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
      auto all_commands = std::vector<std::string_view>(m_commands.begin(), m_commands.end());
      populate_timingcons(this, {
          /*** Channel ***/ 
          // Two-Cycle Commands
          {.level = "channel", .preceding = {"ACT", "RD", "RDA", "WR", "WRA"}, .following = all_commands, .latency = 2},

          // CAS <-> CAS
          /// Data bus occupancy
          {.level = "channel", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA"}, .latency = V("nBL")},
          {.level = "channel", .preceding = {"WR", "WRA"}, .following = {"WR", "WRA"}, .latency = V("nBL")},

          /*** Rank (or different BankGroup) ***/ 
          // CAS <-> CAS
          /// nCCDS is the minimal latency for column commands 
          {.level = "rank", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA"}, .latency = V("nCCDS")},
          {.level = "rank", .preceding = {"WR", "WRA"}, .following = {"WR", "WRA"}, .latency = V("nCCDS_WR")},
          /// RD <-> WR, Minimum Read to Write, Assuming Read DQS Offset = 0, tRPST = 0.5, tWPRE = 2 tCK                          
          {.level = "rank", .preceding = {"RD", "RDA"}, .following = {"WR", "WRA"}, .latency = V("nCL") + V("nBL") + 2 - V("nCWL") + 2},   // nCCDS_RTW
          /// WR <-> RD, Minimum Read after Write
          {.level = "rank", .preceding = {"WR", "WRA"}, .following = {"RD", "RDA"}, .latency = V("nCCDS_WTR")},
          /// CAS <-> CAS between sibling ranks, nCS (rank switching) is needed for new DQS
          {.level = "rank", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA", "WR", "WRA"}, .latency = V("nBL") + V("nCS"), .is_sibling = true},
          {.level = "rank", .preceding = {"WR", "WRA"}, .following = {"RD", "RDA"}, .latency = V("nCL")  + V("nBL") + V("nCS") - V("nCWL"), .is_sibling = true},
          /// CAS <-> PREab
          {.level = "rank", .preceding = {"RD"}, .following = {"PREA"}, .latency = V("nRTP")},
          {.level = "rank", .preceding = {"WR"}, .following = {"PREA"}, .latency = V("nCWL") + V("nBL") + V("nWR")},          
          /// RAS <-> RAS
          {.level = "rank", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRRDS")},          
          {.level = "rank", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nFAW"), .window = 4},          
          {.level = "rank", .preceding = {"ACT"}, .following = {"PREA"}, .latency = V("nRAS")},          
          {.level = "rank", .preceding = {"PREA"}, .following = {"ACT"}, .latency = V("nRP")},          
          /// RAS <-> REF
          {.level = "rank", .preceding = {"ACT"}, .following = {"REFab", "RFMab", "DRFMab"}, .latency = V("nRC")},          
          {.level = "rank", .preceding = {"PRE", "PREA"}, .following = {"REFab", "RFMab", "DRFMab"}, .latency = V("nRP")},          
          {.level = "rank", .preceding = {"RDA"}, .following = {"REFab", "RFMab", "DRFMab"}, .latency = V("nRP") + V("nRTP")},          
          {.level = "rank", .preceding = {"WRA"}, .following = {"REFab", "RFMab", "DRFMab"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},          
          {.level = "rank", .preceding = {"REFab"}, .following = {"ACT"}, .latency = V("nRFC1")},          
          {.level = "rank", .preceding = {"RFMab"}, .following = {"ACT"}, .latency = V("nRFM1")},          
          {.level = "rank", .preceding = {"DRFMab"}, .following = {"ACT"}, .latency = V("nDRFMab")},          

          /*** Same Bank Group ***/ 
          /// CAS <-> CAS
          {.level = "bankgroup", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA"}, .latency = V("nCCDL")},          
          {.level = "bankgroup", .preceding = {"WR", "WRA"}, .following = {"WR", "WRA"}, .latency = V("nCCDL_WR")},          
          {.level = "bankgroup", .preceding = {"WR", "WRA"}, .following = {"RD", "RDA"}, .latency = V("nCCDL_WTR")},
          /// RAS <-> RAS
          {.level = "bankgroup", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRRDL")},  

          /*** Bank ***/ 
          {.level = "bank", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRC")},  
          {.level = "bank", .preceding = {"ACT"}, .following = {"RD", "RDA", "WR", "WRA"}, .latency = V("nRCD")},  
          {.level = "bank", .preceding = {"ACT"}, .following = {"PRE"}, .latency = V("nRAS")},  
          {.level = "bank", .preceding = {"PRE"}, .following = {"ACT"}, .latency = V("nRP")},  
          {.level = "bank", .preceding = {"RD"},  .following = {"PRE"}, .latency = V("nRTP")},  
          {.level = "bank", .preceding = {"WR"},  .following = {"PRE"}, .latency = V("nCWL") + V("nBL") + V("nWR")},  
          {.level = "bank", .preceding = {"RDA"}, .following = {"ACT"}, .latency = V("nRTP") + V("nRP")},  
          {.level = "bank", .preceding = {"WRA"}, .following = {"ACT"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},  
          {.level = "bank", .preceding = {"WR"},  .following = {"RDA"}, .latency = V("nCWL") + V("nBL") + V("nWR") - V("nRTP")},  

          /// Same-bank refresh/RFM timings. The timings of the bank in other BGs will be updated by action function
          {.level = "bank", .preceding = {"REFsb"},  .following = {"ACT", "REFsb", "RFMsb", "DRFMsb"}, .latency = V("nRFCsb")},  
          {.level = "bank", .preceding = {"RFMsb"},  .following = {"ACT", "REFsb", "RFMsb", "DRFMsb"}, .latency = V("nRFMsb")},  
          {.level = "bank", .preceding = {"DRFMsb"}, .following = {"ACT", "REFsb", "RFMsb", "DRFMsb"}, .latency = V("nDRFMsb")},  
        }
      );
      #undef V

    };

    void set_actions() {
      m_actions.resize(m_levels.size(), std::vector<ActionFunc_t<Node>>(m_commands.size()));

      // Rank Actions
      m_actions[m_levels["rank"]][m_commands["PREA"]] = Lambdas::Action::Rank::PREab<DDR5>;

      // Same-Bank Actions.
      m_actions[m_levels["bankgroup"]][m_commands["PREsb"]] = Lambdas::Action::BankGroup::PREsb<DDR5>;

      // We call update_timing for the banks in other BGs here
      m_actions[m_levels["bankgroup"]][m_commands["REFsb"]]  = Lambdas::Action::BankGroup::SameBankActions<DDR5>;
      m_actions[m_levels["bankgroup"]][m_commands["RFMsb"]]  = Lambdas::Action::BankGroup::SameBankActions<DDR5>;
      m_actions[m_levels["bankgroup"]][m_commands["DRFMsb"]] = Lambdas::Action::BankGroup::SameBankActions<DDR5>;

      // Bank actions
      m_actions[m_levels["bank"]][m_commands["ACT"]] = Lambdas::Action::Bank::ACT<DDR5>;
      m_actions[m_levels["bank"]][m_commands["PRE"]] = Lambdas::Action::Bank::PRE<DDR5>;
      m_actions[m_levels["bank"]][m_commands["RDA"]] = Lambdas::Action::Bank::PRE<DDR5>;
      m_actions[m_levels["bank"]][m_commands["WRA"]] = Lambdas::Action::Bank::PRE<DDR5>;
    };

    void set_preqs() {
      m_preqs.resize(m_levels.size(), std::vector<PreqFunc_t<Node>>(m_commands.size()));

      // Rank Preqs
      m_preqs[m_levels["rank"]][m_commands["REFab"]]  = Lambdas::Preq::Rank::RequireAllBanksClosed<DDR5>;
      m_preqs[m_levels["rank"]][m_commands["RFMab"]]  = Lambdas::Preq::Rank::RequireAllBanksClosed<DDR5>;
      m_preqs[m_levels["rank"]][m_commands["DRFMab"]] = Lambdas::Preq::Rank::RequireAllBanksClosed<DDR5>;

      // Same-Bank Preqs.
      m_preqs[m_levels["bankgroup"]][m_commands["REFsb"]]  = Lambdas::Preq::Rank::RequireSameBanksClosed<DDR5>;
      m_preqs[m_levels["bankgroup"]][m_commands["RFMsb"]]  = Lambdas::Preq::Rank::RequireSameBanksClosed<DDR5>;
      m_preqs[m_levels["bankgroup"]][m_commands["DRFMsb"]] = Lambdas::Preq::Rank::RequireSameBanksClosed<DDR5>;

      // Bank Preqs
      m_preqs[m_levels["bank"]][m_commands["RD"]] = Lambdas::Preq::Bank::RequireRowOpen<DDR5>;
      m_preqs[m_levels["bank"]][m_commands["WR"]] = Lambdas::Preq::Bank::RequireRowOpen<DDR5>;
    };

    void set_rowhits() {
      m_rowhits.resize(m_levels.size(), std::vector<RowhitFunc_t<Node>>(m_commands.size()));

      m_rowhits[m_levels["bank"]][m_commands["RD"]] = Lambdas::RowHit::Bank::RDWR<DDR5>;
      m_rowhits[m_levels["bank"]][m_commands["WR"]] = Lambdas::RowHit::Bank::RDWR<DDR5>;
    }


    void set_rowopens() {
      m_rowopens.resize(m_levels.size(), std::vector<RowhitFunc_t<Node>>(m_commands.size()));

      m_rowopens[m_levels["bank"]][m_commands["RD"]] = Lambdas::RowOpen::Bank::RDWR<DDR5>;
      m_rowopens[m_levels["bank"]][m_commands["WR"]] = Lambdas::RowOpen::Bank::RDWR<DDR5>;
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
