#include "dram/dram.h"
#include "dram/lambdas.h"

namespace Ramulator {

class DDR5RVRR : public IDRAM, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IDRAM, DDR5RVRR, "DDR5-RVRR", "DDR5 with Reduced Victim Row Refresh")
  private:
    int m_RH_radius = -1;

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
      //   name         rate   nBL  nCL nRCD   nRP  nRAS   nRC   nWR  nRTP nCWL nPPD nCCDS nCCDS_WR nCCDS_WTR nCCDL nCCDL_WR nCCDL_WTR nRRDS nRRDL nFAW nRFC1 nRFC2 nRFCsb nREFI nREFSBRD nRFM1 nRFM2 nRFMsb nRRFMsb nDRFMab nDRFMsb nVRR nRVRR nCS, tCK_ps
      {"DDR5_3200AN",  {3200,   8,  24,  24,   24,   52,   75,   48,   12,  22,  2,    8,     8,     22+8+4,    8,     16,    22+8+16,   8,   -1,   -1,  -1,   -1,   -1,    -1,     30,    -1,   -1,   -1,     -1,     -1,     -1,    -1,   -1,  2,   625}},
      {"DDR5_3200BN",  {3200,   8,  26,  26,   26,   52,   77,   48,   12,  24,  2,    8,     8,     24+8+4,    8,     16,    24+8+16,   8,   -1,   -1,  -1,   -1,   -1,    -1,     30,    -1,   -1,   -1,     -1,     -1,     -1,    -1,   -1,  2,   625}},
      {"DDR5_3200C",   {3200,   8,  28,  28,   28,   52,   79,   48,   12,  26,  2,    8,     8,     26+8+4,    8,     16,    26+8+16,   8,   -1,   -1,  -1,   -1,   -1,    -1,     30,    -1,   -1,   -1,     -1,     -1,     -1,    -1,   -1,  2,   625}},
    };

    inline static const std::map<std::string, std::vector<double>> voltage_presets = {
      //   name          VDD      VPP
      {"Default",       {1.1,     1.8}},
    };

    inline static const std::map<std::string, std::vector<double>> current_presets = {
      // name           IDD0  IDD2N   IDD3N   IDD4R   IDD4W   IDD5B   IPP0  IPP2N  IPP3N  IPP4R  IPP4W  IPP5B
      {"Default",       {60,   50,     55,     145,    145,    362,     3,    3,     3,     3,     3,     48}},
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
      "REFab",  "REFsb", "REFab_end", "REFsb_end",
      "RFMab",  "RFMsb", "RFMab_end", "RFMsb_end",
      "DRFMab", "DRFMsb", "DRFMab_end", "DRFMsb_end",
      "RRFMsb", "RRFMsb_end",
      "VRR", "VRR_end",
      "RVRR", "RVRR_end",
    };

    inline static const ImplLUT m_command_scopes = LUT (
      m_commands, m_levels, {
        {"ACT",   "row"},
        {"PRE",   "bank"},   {"PREA",   "rank"},   {"PREsb", "bank"},
        {"RD",    "column"}, {"WR",     "column"}, {"RDA",   "column"}, {"WRA",   "column"},
        {"REFab",  "rank"},  {"REFsb",  "bank"}, {"REFab_end",  "rank"},  {"REFsb_end",  "bank"},
        {"RFMab",  "rank"},  {"RFMsb",  "bank"}, {"RFMab_end",  "rank"},  {"RFMsb_end",  "bank"},
        {"DRFMab", "rank"},  {"DRFMsb", "bank"}, {"DRFMab_end", "rank"},  {"DRFMsb_end", "bank"},
        {"RRFMsb", "bank"}, {"RRFMsb_end", "bank"},
        {"VRR",   "bank"},   {"VRR_end",   "bank"},
        {"RVRR",  "bank"},   {"RVRR_end",  "bank"},
      }
    );

    inline static const ImplLUT m_command_meta = LUT<DRAMCommandMeta> (
      m_commands, {
                      // open?   close?   access?  refresh?
        {"ACT",         {true,   false,   false,   false}},
        {"PRE",         {false,  true,    false,   false}},
        {"PREA",        {false,  true,    false,   false}},
        {"PREsb",       {false,  true,    false,   false}},
        {"RD",          {false,  false,   true,    false}},
        {"WR",          {false,  false,   true,    false}},
        {"RDA",         {false,  true,    true,    false}},
        {"WRA",         {false,  true,    true,    false}},
        {"REFab",       {false,  false,   false,   true }},
        {"REFsb",       {false,  false,   false,   true }},
        {"REFab_end",   {false,  true,    false,   false}},
        {"REFsb_end",   {false,  true,    false,   false}},
        {"RFMab",       {false,  false,   false,   true }},
        {"RFMsb",       {false,  false,   false,   true }},
        {"RFMab_end",   {false,  true,    false,   false}},
        {"RFMsb_end",   {false,  true,    false,   false}},
        {"DRFMab",      {false,  false,   false,   true }},
        {"DRFMsb",      {false,  false,   false,   true }},
        {"DRFMab_end",  {false,  true,    false,   false}},
        {"DRFMsb_end",  {false,  true,    false,   false}},
        {"RRFMsb",      {false,  false,   false,   true }},
        {"RRFMsb_end",  {false,  true,    false,   false}},
        {"VRR",         {false,  false,   false,   true }},
        {"VRR_end",     {false,  true,    false,   false}},
        {"RVRR",        {false,  false,   false,   true }},
        {"RVRR_end",    {false,  true,    false,   false}},
      }
    );

    inline static constexpr ImplDef m_requests = {
      "read", "write", 
      "all-bank-refresh", "same-bank-refresh", 
      "rfm", "same-bank-rfm",
      "directed-rfm", "same-bank-directed-rfm",
      "reduced-same-bank-rfm",
      "victim-row-refresh",
      "reduced-victim-row-refresh", "open-row", "close-row"
    };

    inline static const ImplLUT m_request_translations = LUT (
      m_requests, m_commands, {
        {"read", "RD"}, {"write", "WR"}, 
        {"all-bank-refresh", "REFab"}, {"same-bank-refresh", "REFsb"}, 
        {"rfm", "RFMab"}, {"same-bank-rfm", "RFMsb"}, 
        {"directed-rfm", "DRFMab"}, {"same-bank-directed-rfm", "DRFMsb"},
        {"reduced-same-bank-rfm", "RRFMsb"}, 
        {"victim-row-refresh", "VRR"},
        {"reduced-victim-row-refresh", "RVRR"}, {"open-row", "ACT"}, {"close-row", "PRE"}
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
      "nRFM1", "nRFM2", "nRFMsb", "nRRFMsb",
      "nDRFMab", "nDRFMsb", 
      "nVRR",
      "nRVRR",
      "nCS",
      "tCK_ps"
    };

/************************************************
   *                   Power
   ***********************************************/
    inline static constexpr ImplDef m_voltages = {
      "VDD", "VPP"
    };
    
    inline static constexpr ImplDef m_currents = {
      "IDD0", "IDD2N", "IDD3N", "IDD4R", "IDD4W", "IDD5B",
      "IPP0", "IPP2N", "IPP3N", "IPP4R", "IPP4W", "IPP5B"
    };

    inline static constexpr ImplDef m_cmds_counted = {
      "ACT", "PRE", "RD", "WR", "REF", "RFM", "VRR", "RRFM", "RVRR"
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
        {"bankgroup", "N/A"},
        {"bank",      "Closed"},
        {"row",       "Closed"},
        {"column",    "N/A"},
      }
    );

  public:
    struct Node : public DRAMNodeBase<DDR5RVRR> {
      Node(DDR5RVRR* dram, Node* parent, int level, int id) : DRAMNodeBase<DDR5RVRR>(dram, parent, level, id) {};
    };
    std::vector<Node*> m_channels;
    
    FuncMatrix<ActionFunc_t<Node>>  m_actions;
    FuncMatrix<PreqFunc_t<Node>>    m_preqs;
    FuncMatrix<RowhitFunc_t<Node>>  m_rowhits;
    FuncMatrix<RowopenFunc_t<Node>> m_rowopens;
    FuncMatrix<PowerFunc_t<Node>>   m_powers;
    float m_latency_factor_vrr = 1.0f;
    float m_latency_factor_rfc = 1.0f;

    double s_total_rfm_energy = 0.0;
    double s_total_rrfm_energy = 0.0;
    double s_total_vrr_energy = 0.0;
    double s_total_rvrr_energy = 0.0;

    std::vector<size_t> s_total_vrr_cycles;
    std::vector<size_t> s_total_rvrr_cycles;
    std::vector<size_t> s_total_rfm_cycles;
    std::vector<size_t> s_total_rrfm_cycles;
    
  /************************************************
   *                 RFM Related
   ***********************************************/
  public:
    int m_BRC = 2;


  public:
    void tick() override {
      m_clk++;

      // Check if there is any future action at this cycle
      for (int i = m_future_actions.size() - 1; i >= 0; i--) {
        auto& future_action = m_future_actions[i];
        if (future_action.clk == m_clk) {
          handle_future_action(future_action.cmd, future_action.addr_vec);
          m_future_actions.erase(m_future_actions.begin() + i);
        }
      }
    };

    void init() override {
      RAMULATOR_DECLARE_SPECS();
      m_latency_factor_vrr = param<float>("latency_factor_vrr").desc("Factor to scale the latency of the DRAM.").default_val(1.0f);
      m_latency_factor_rfc = param<float>("latency_factor_rfc").desc("Factor to scale the latency of the DRAM.").default_val(1.0f);
      set_organization();
      set_timing_vals();

      set_actions();
      set_preqs();
      set_rowhits();
      set_rowopens();
      set_powers();
      
      create_nodes();
    };

    void issue_command(int command, const AddrVec_t& addr_vec) override {
            int channel_id = addr_vec[m_levels["channel"]];
      m_channels[channel_id]->update_timing(command, addr_vec, m_clk);
      m_channels[channel_id]->update_powers(command, addr_vec, m_clk);
      m_channels[channel_id]->update_states(command, addr_vec, m_clk);

                  // Check if the command requires future action
      check_future_action(command, addr_vec);
    };

    void check_future_action(int command, const AddrVec_t& addr_vec) {
      switch (command) {
        case m_commands("REFab"):
          m_future_actions.push_back({command, addr_vec, m_clk + m_timing_vals("nRFC1") - 1});
          break;
        case m_commands("REFsb"):
          m_future_actions.push_back({command, addr_vec, m_clk + m_timing_vals("nRFCsb") - 1});
          break;
        case m_commands("RFMab"):
          m_future_actions.push_back({command, addr_vec, m_clk + m_timing_vals("nRFM1") - 1});
          break;
        case m_commands("RFMsb"):
          m_future_actions.push_back({command, addr_vec, m_clk + m_timing_vals("nRFMsb") - 1});
          break;
        case m_commands("DRFMab"):
          m_future_actions.push_back({command, addr_vec, m_clk + m_timing_vals("nDRFMab") - 1});
          break;
        case m_commands("DRFMsb"):
          m_future_actions.push_back({command, addr_vec, m_clk + m_timing_vals("nDRFMsb") - 1});
          break;
        case m_commands("RRFMsb"):
          m_future_actions.push_back({command, addr_vec, m_clk + m_timing_vals("nRRFMsb") - 1});
          break;
        case m_commands("VRR"):
          m_future_actions.push_back({command, addr_vec, m_clk + m_timing_vals("nVRR") - 1});
          break;
        case m_commands("RVRR"):
          m_future_actions.push_back({command, addr_vec, m_clk + m_timing_vals("nRVRR") - 1});
          break;
        default:
          // Other commands do not require future actions
          break;
      }
    }

    void handle_future_action(int command, const AddrVec_t& addr_vec) {
      int channel_id = addr_vec[m_levels["channel"]];
      switch (command) {
        case m_commands("REFab"):
          m_channels[channel_id]->update_powers(m_commands("REFab_end"), addr_vec, m_clk);
          m_channels[channel_id]->update_states(m_commands("REFab_end"), addr_vec, m_clk);
                    break;
        case m_commands("REFsb"):
          m_channels[channel_id]->update_powers(m_commands("REFsb_end"), addr_vec, m_clk);
          m_channels[channel_id]->update_states(m_commands("REFsb_end"), addr_vec, m_clk);
                    break;
        case m_commands("RFMab"):
          m_channels[channel_id]->update_powers(m_commands("RFMab_end"), addr_vec, m_clk);
          m_channels[channel_id]->update_states(m_commands("RFMab_end"), addr_vec, m_clk);
          break;
        case m_commands("RFMsb"):
          m_channels[channel_id]->update_powers(m_commands("RFMsb_end"), addr_vec, m_clk);
          m_channels[channel_id]->update_states(m_commands("RFMsb_end"), addr_vec, m_clk);
                    break;
        case m_commands("DRFMab"):
          m_channels[channel_id]->update_powers(m_commands("DRFMab_end"), addr_vec, m_clk);
          m_channels[channel_id]->update_states(m_commands("DRFMab_end"), addr_vec, m_clk);
          break;
        case m_commands("DRFMsb"):
          m_channels[channel_id]->update_powers(m_commands("DRFMsb_end"), addr_vec, m_clk);
          m_channels[channel_id]->update_states(m_commands("DRFMsb_end"), addr_vec, m_clk);
          break;
        case m_commands("RRFMsb"):
          m_channels[channel_id]->update_powers(m_commands("RRFMsb_end"), addr_vec, m_clk);
          m_channels[channel_id]->update_states(m_commands("RRFMsb_end"), addr_vec, m_clk);
                    break;
        case m_commands("VRR"):
          m_channels[channel_id]->update_powers(m_commands("VRR_end"), addr_vec, m_clk);
          m_channels[channel_id]->update_states(m_commands("VRR_end"), addr_vec, m_clk);
                    break;
        case m_commands("RVRR"):
          m_channels[channel_id]->update_powers(m_commands("RVRR_end"), addr_vec, m_clk);
          m_channels[channel_id]->update_states(m_commands("RVRR_end"), addr_vec, m_clk);
                    break;
        default:
          // Other commands do not require future actions
          break;
      }
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
      int num_channels = m_organization.count[m_levels["channel"]];
      int num_ranks = m_organization.count[m_levels["rank"]];
      s_total_rfm_cycles.resize(num_channels * num_ranks, 0);
      s_total_rrfm_cycles.resize(num_channels * num_ranks, 0);
      s_total_vrr_cycles.resize(num_channels * num_ranks, 0);
      s_total_rvrr_cycles.resize(num_channels * num_ranks, 0);

      for (int r = 0; r < num_channels * num_ranks; r++) {
        register_stat(s_total_rfm_cycles[r]).name("total_rfm_cycles_rank{}", r);
        register_stat(s_total_rrfm_cycles[r]).name("total_rrfm_cycles_rank{}", r);
        register_stat(s_total_vrr_cycles[r]).name("total_vrr_cycles_rank{}", r);
        register_stat(s_total_rvrr_cycles[r]).name("total_rvrr_cycles_rank{}", r);
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

      m_RH_radius = param<int>("RH_radius").desc("The number of rows to refresh on each side").default_val(2);
      
      m_timing_vals("nRFC1")  = JEDEC_rounding_DDR5(tRFC_TABLE[0][density_id], tCK_ps);
      m_timing_vals("nRFC2")  = JEDEC_rounding_DDR5(tRFC_TABLE[1][density_id], tCK_ps);
      m_timing_vals("nRFCsb") = JEDEC_rounding_DDR5(tRFCsb_TABLE[0][density_id], tCK_ps);
      m_timing_vals("nREFI")  = JEDEC_rounding_DDR5(tREFI_BASE, tCK_ps);

      m_timing_vals("nRFM1")  = m_timing_vals("nRFC1");
      m_timing_vals("nRFM2")  = m_timing_vals("nRFC2");
      m_timing_vals("nRFMsb") = m_timing_vals("nRFCsb") * m_RH_radius;

      const int nVRR_base_ns = 70;   // Taken from DDR5 DRFM
      m_timing_vals("nVRR") = JEDEC_rounding_DDR5(nVRR_base_ns * m_RH_radius * 2, tCK_ps);
      m_timing_vals("nRVRR") = m_latency_factor_vrr * ((float)m_timing_vals("nVRR") * (float)m_timing_vals("nRAS") / (float)(m_timing_vals("nRAS") + m_timing_vals("nRP")))
                                + ((float)m_timing_vals("nVRR") * (float)m_timing_vals("nRP") / (float)(m_timing_vals("nRAS") + m_timing_vals("nRP")));
      m_timing_vals("nRRFMsb") = m_latency_factor_vrr * ((float)m_timing_vals("nRFMsb") * (float)m_timing_vals("nRAS") / (float)(m_timing_vals("nRAS") + m_timing_vals("nRP")))
                                + ((float)m_timing_vals("nRFMsb") * (float)m_timing_vals("nRP") / (float)(m_timing_vals("nRAS") + m_timing_vals("nRP")));

      // tRRF table (unit is nanosecond!)
      constexpr int tRRFsb_TABLE[2][3] = {
      //  8Gb 16Gb 32Gb  
        { 70,  70,  70 }, // tRRFab
        { 60,  60,  60 }, // tRRFsb
      };
      m_BRC = param_group("RFM").param<int>("BRC").default_val(2);
      m_timing_vals("nDRFMab") = 2 * m_BRC * JEDEC_rounding_DDR5(tRRFsb_TABLE[0][density_id], tCK_ps);
      m_timing_vals("nDRFMsb") = 2 * m_BRC * JEDEC_rounding_DDR5(tRRFsb_TABLE[1][density_id], tCK_ps);


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

      // apply latency factor
      std::cout << "latency_factor_vrr: " << m_latency_factor_vrr << std::endl;
      std::cout << "latency_factor_rfc: " << m_latency_factor_rfc << std::endl;
      std::cout << "Default Parameters: " << std::endl;
      std::cout << "nRFC1: " << m_timing_vals("nRFC1") << std::endl;
      if (m_latency_factor_rfc != 1.0f)
        m_timing_vals("nRFC1") = m_latency_factor_rfc * ((float)m_timing_vals("nRFC1") * (float)m_timing_vals("nRAS") / (float)(m_timing_vals("nRAS") + m_timing_vals("nRP")))
                              + ((float)m_timing_vals("nRFC1") * (float)m_timing_vals("nRP") / (float)(m_timing_vals("nRAS") + m_timing_vals("nRP")));
                              
      std::cout << "Used Parameters: " << std::endl;
      std::cout << "nRAS: " << m_timing_vals("nRAS") << std::endl;
      std::cout << "nRP: " << m_timing_vals("nRP") << std::endl;
      std::cout << "m_RH_radius: " << m_RH_radius << std::endl;
      std::cout << "nVRR: " << m_timing_vals("nVRR") << std::endl;
      std::cout << "nRVRR: " << m_timing_vals("nRVRR") << std::endl;
      std::cout << "nRFMsb: " << m_timing_vals("nRFMsb") << std::endl;
      std::cout << "nRRFMsb: " << m_timing_vals("nRRFMsb") << std::endl;
      std::cout << "nRFC1: " << m_timing_vals("nRFC1") << std::endl;

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
          {.level = "rank", .preceding = {"VRR"}, .following = {"PREA"}, .latency = V("nVRR")},          
          {.level = "rank", .preceding = {"RVRR"}, .following = {"PREA"}, .latency = V("nRVRR")},          
          {.level = "rank", .preceding = {"PREA"}, .following = {"ACT", "VRR", "RVRR"}, .latency = V("nRP")},          
          /// RAS <-> REF
          {.level = "rank", .preceding = {"ACT"}, .following = {"REFab", "RFMab", "DRFMab"}, .latency = V("nRC")},          
          {.level = "rank", .preceding = {"VRR"}, .following = {"REFab", "RFMab", "DRFMab"}, .latency = V("nVRR")},          
          {.level = "rank", .preceding = {"RVRR"}, .following = {"REFab", "RFMab", "DRFMab"}, .latency = V("nRVRR")},          
          {.level = "rank", .preceding = {"PRE", "PREsb"}, .following = {"REFab", "RFMab", "DRFMab"}, .latency = V("nRP")},          
          {.level = "rank", .preceding = {"PREA"}, .following = {"REFab", "RFMab", "DRFMab", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nRP")},          
          {.level = "rank", .preceding = {"RDA"}, .following = {"REFab", "RFMab", "DRFMab"}, .latency = V("nRP") + V("nRTP")},          
          {.level = "rank", .preceding = {"WRA"}, .following = {"REFab", "RFMab", "DRFMab"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},          
          {.level = "rank", .preceding = {"REFab"}, .following = {"ACT", "VRR", "RVRR", "PREA", "REFab", "RFMab", "DRFMab", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nRFC1")},          
          {.level = "rank", .preceding = {"RFMab"}, .following = {"ACT", "VRR", "RVRR", "PREA", "REFab", "RFMab", "DRFMab", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nRFM1")},          
          {.level = "rank", .preceding = {"DRFMab"}, .following = {"ACT", "VRR", "RVRR", "PREA", "REFab", "RFMab", "DRFMab", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nDRFMab")},          
          {.level = "rank", .preceding = {"REFsb"},  .following = {"PREA", "REFab", "RFMab", "DRFMab"}, .latency = V("nRFCsb")},  
          {.level = "rank", .preceding = {"RFMsb"},  .following = {"PREA", "REFab", "RFMab", "DRFMab"}, .latency = V("nRFMsb")},  
          {.level = "rank", .preceding = {"DRFMsb"}, .following = {"PREA", "REFab", "RFMab", "DRFMab"}, .latency = V("nDRFMsb")},  
          {.level = "rank", .preceding = {"RRFMsb"},  .following = {"PREA", "REFab", "RFMab", "DRFMab"}, .latency = V("nRRFMsb")},  
          /*** Same Bank Group ***/ 
          /// CAS <-> CAS
          {.level = "bankgroup", .preceding = {"RD", "RDA"}, .following = {"RD", "RDA"}, .latency = V("nCCDL")},          
          {.level = "bankgroup", .preceding = {"WR", "WRA"}, .following = {"WR", "WRA"}, .latency = V("nCCDL_WR")},          
          {.level = "bankgroup", .preceding = {"WR", "WRA"}, .following = {"RD", "RDA"}, .latency = V("nCCDL_WTR")},
          /// RAS <-> RAS
          {.level = "bankgroup", .preceding = {"ACT"}, .following = {"ACT"}, .latency = V("nRRDL")},  

          /*** Bank ***/ 
          {.level = "bank", .preceding = {"ACT"}, .following = {"ACT", "VRR", "RVRR", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nRC")},  
          {.level = "bank", .preceding = {"VRR"}, .following = {"ACT", "VRR", "RVRR", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nVRR")},  
          {.level = "bank", .preceding = {"RVRR"}, .following = {"ACT", "VRR", "RVRR", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nRVRR")},  
          {.level = "bank", .preceding = {"ACT"}, .following = {"RD", "RDA", "WR", "WRA"}, .latency = V("nRCD")},  
          {.level = "bank", .preceding = {"ACT"}, .following = {"PRE", "PREsb"}, .latency = V("nRAS")},  
          {.level = "bank", .preceding = {"PRE", "PREsb"}, .following = {"ACT", "VRR", "RVRR", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nRP")},  
          {.level = "bank", .preceding = {"RD"},  .following = {"PRE", "PREsb"}, .latency = V("nRTP")},  
          {.level = "bank", .preceding = {"WR"},  .following = {"PRE", "PREsb"}, .latency = V("nCWL") + V("nBL") + V("nWR")},  
          {.level = "bank", .preceding = {"RDA"}, .following = {"ACT", "VRR", "RVRR", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nRTP") + V("nRP")},  
          {.level = "bank", .preceding = {"WRA"}, .following = {"ACT", "VRR", "RVRR", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nCWL") + V("nBL") + V("nWR") + V("nRP")},  
          {.level = "bank", .preceding = {"WR"},  .following = {"RDA"}, .latency = V("nCWL") + V("nBL") + V("nWR") - V("nRTP")},  

          /// Same-bank refresh/RFM timings. The timings of the bank in other BGs will be updated by action function
          {.level = "bank", .preceding = {"REFsb"},  .following = {"ACT", "VRR", "RVRR", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nRFCsb")},  
          {.level = "bank", .preceding = {"RFMsb"},  .following = {"ACT", "VRR", "RVRR", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nRFMsb")},  
          {.level = "bank", .preceding = {"DRFMsb"}, .following = {"ACT", "VRR", "RVRR", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nDRFMsb")},  
          {.level = "bank", .preceding = {"RRFMsb"},  .following = {"ACT", "VRR", "RVRR", "REFsb", "RFMsb", "DRFMsb", "RRFMsb"}, .latency = V("nRRFMsb")},  
        }
      );
      #undef V

    };

    void set_actions() {
      m_actions.resize(m_levels.size(), std::vector<ActionFunc_t<Node>>(m_commands.size()));

      // Rank Actions
      m_actions[m_levels["rank"]][m_commands["PREA"]] = Lambdas::Action::Rank::PREab<DDR5RVRR>;
      m_actions[m_levels["rank"]][m_commands["REFab"]] = Lambdas::Action::Rank::REFab<DDR5RVRR>;
      m_actions[m_levels["rank"]][m_commands["REFab_end"]] = Lambdas::Action::Rank::REFab_end<DDR5RVRR>;
      m_actions[m_levels["rank"]][m_commands["RFMab"]] = Lambdas::Action::Rank::REFab<DDR5RVRR>;
      m_actions[m_levels["rank"]][m_commands["RFMab_end"]] = Lambdas::Action::Rank::REFab_end<DDR5RVRR>;
      m_actions[m_levels["rank"]][m_commands["DRFMab"]] = Lambdas::Action::Rank::REFab<DDR5RVRR>;
      m_actions[m_levels["rank"]][m_commands["DRFMab_end"]] = Lambdas::Action::Rank::REFab_end<DDR5RVRR>;

      // Same-Bank Actions.
      m_actions[m_levels["bankgroup"]][m_commands["PREsb"]] = Lambdas::Action::BankGroup::PREsb<DDR5RVRR>;

      // We call update_timing for the banks in other BGs here
      m_actions[m_levels["bankgroup"]][m_commands["REFsb"]]  = Lambdas::Action::BankGroup::REFsb<DDR5RVRR>;
      m_actions[m_levels["bankgroup"]][m_commands["REFsb_end"]]  = Lambdas::Action::BankGroup::REFsb_end<DDR5RVRR>;
      m_actions[m_levels["bankgroup"]][m_commands["RFMsb"]]  = Lambdas::Action::BankGroup::REFsb<DDR5RVRR>;
      m_actions[m_levels["bankgroup"]][m_commands["RFMsb_end"]]  = Lambdas::Action::BankGroup::REFsb_end<DDR5RVRR>;
      m_actions[m_levels["bankgroup"]][m_commands["DRFMsb"]] = Lambdas::Action::BankGroup::REFsb<DDR5RVRR>;
      m_actions[m_levels["bankgroup"]][m_commands["DRFMsb_end"]] = Lambdas::Action::BankGroup::REFsb_end<DDR5RVRR>;
      m_actions[m_levels["bankgroup"]][m_commands["RRFMsb"]] = Lambdas::Action::BankGroup::REFsb<DDR5RVRR>;
      m_actions[m_levels["bankgroup"]][m_commands["RRFMsb_end"]] = Lambdas::Action::BankGroup::REFsb_end<DDR5RVRR>;

      // Bank actions
      m_actions[m_levels["bank"]][m_commands["ACT"]] = Lambdas::Action::Bank::ACT<DDR5RVRR>;
      m_actions[m_levels["bank"]][m_commands["PRE"]] = Lambdas::Action::Bank::PRE<DDR5RVRR>;
      m_actions[m_levels["bank"]][m_commands["RDA"]] = Lambdas::Action::Bank::PRE<DDR5RVRR>;
      m_actions[m_levels["bank"]][m_commands["WRA"]] = Lambdas::Action::Bank::PRE<DDR5RVRR>;
      m_actions[m_levels["bank"]][m_commands["VRR"]] = Lambdas::Action::Bank::VRR<DDR5RVRR>;
      m_actions[m_levels["bank"]][m_commands["VRR_end"]] = Lambdas::Action::Bank::VRR_end<DDR5RVRR>;
      m_actions[m_levels["bank"]][m_commands["RVRR"]] = Lambdas::Action::Bank::VRR<DDR5RVRR>;
      m_actions[m_levels["bank"]][m_commands["RVRR_end"]] = Lambdas::Action::Bank::VRR_end<DDR5RVRR>;
    };

    void set_preqs() {
      m_preqs.resize(m_levels.size(), std::vector<PreqFunc_t<Node>>(m_commands.size()));

      // Rank Preqs
      m_preqs[m_levels["rank"]][m_commands["REFab"]]  = Lambdas::Preq::Rank::RequireAllBanksClosed<DDR5RVRR>;
      m_preqs[m_levels["rank"]][m_commands["RFMab"]]  = Lambdas::Preq::Rank::RequireAllBanksClosed<DDR5RVRR>;
      m_preqs[m_levels["rank"]][m_commands["DRFMab"]] = Lambdas::Preq::Rank::RequireAllBanksClosed<DDR5RVRR>;

      // Same-Bank Preqs.
      m_preqs[m_levels["rank"]][m_commands["REFsb"]]  = Lambdas::Preq::Rank::RequireSameBanksClosed<DDR5RVRR>;
      m_preqs[m_levels["rank"]][m_commands["RFMsb"]]  = Lambdas::Preq::Rank::RequireSameBanksClosed<DDR5RVRR>;
      m_preqs[m_levels["rank"]][m_commands["DRFMsb"]] = Lambdas::Preq::Rank::RequireSameBanksClosed<DDR5RVRR>;
      m_preqs[m_levels["rank"]][m_commands["RRFMsb"]] = Lambdas::Preq::Rank::RequireSameBanksClosed<DDR5RVRR>;

      // Bank Preqs
      m_preqs[m_levels["bank"]][m_commands["RD"]] = Lambdas::Preq::Bank::RequireRowOpen<DDR5RVRR>;
      m_preqs[m_levels["bank"]][m_commands["WR"]] = Lambdas::Preq::Bank::RequireRowOpen<DDR5RVRR>;
      m_preqs[m_levels["bank"]][m_commands["VRR"]] = Lambdas::Preq::Bank::RequireBankClosed<DDR5RVRR>;
      m_preqs[m_levels["bank"]][m_commands["RVRR"]] = Lambdas::Preq::Bank::RequireBankClosed<DDR5RVRR>;
      m_preqs[m_levels["bank"]][m_commands["ACT"]] = Lambdas::Preq::Bank::RequireRowOpen<DDR5RVRR>;
      m_preqs[m_levels["bank"]][m_commands["PRE"]] = Lambdas::Preq::Bank::RequireBankClosed<DDR5RVRR>;

    };

    void set_rowhits() {
      m_rowhits.resize(m_levels.size(), std::vector<RowhitFunc_t<Node>>(m_commands.size()));

      m_rowhits[m_levels["bank"]][m_commands["RD"]] = Lambdas::RowHit::Bank::RDWR<DDR5RVRR>;
      m_rowhits[m_levels["bank"]][m_commands["WR"]] = Lambdas::RowHit::Bank::RDWR<DDR5RVRR>;
    }


    void set_rowopens() {
      m_rowopens.resize(m_levels.size(), std::vector<RowhitFunc_t<Node>>(m_commands.size()));

      m_rowopens[m_levels["bank"]][m_commands["RD"]] = Lambdas::RowOpen::Bank::RDWR<DDR5RVRR>;
      m_rowopens[m_levels["bank"]][m_commands["WR"]] = Lambdas::RowOpen::Bank::RDWR<DDR5RVRR>;
    }

    void set_powers() {
      
      m_drampower_enable = param<bool>("drampower_enable").default_val(false);

      if (!m_drampower_enable)
        return;

      m_voltage_vals.resize(m_voltages.size(), -1);

      if (auto preset_name = param_group("voltage").param<std::string>("preset").optional()) {
        if (voltage_presets.count(*preset_name) > 0) {
          m_voltage_vals = voltage_presets.at(*preset_name);
        } else {
          throw ConfigurationError("Unrecognized voltage preset \"{}\" in {}!", *preset_name, get_name());
        }
      }

      m_current_vals.resize(m_currents.size(), -1);

      if (auto preset_name = param_group("current").param<std::string>("preset").optional()) {
        if (current_presets.count(*preset_name) > 0) {
          m_current_vals = current_presets.at(*preset_name);
        } else {
          throw ConfigurationError("Unrecognized current preset \"{}\" in {}!", *preset_name, get_name());
        }
      }

      m_power_debug = param<bool>("power_debug").default_val(false);

      // TODO: Check for multichannel configs.
      int num_channels = m_organization.count[m_levels["channel"]];
      int num_ranks = m_organization.count[m_levels["rank"]];
      m_power_stats.resize(num_channels * num_ranks);
      for (int i = 0; i < num_channels; i++) {
        for (int j = 0; j < num_ranks; j++) {
          m_power_stats[i * num_ranks + j].rank_id = i * num_ranks + j;
          m_power_stats[i * num_ranks + j].cmd_counters.resize(m_cmds_counted.size(), 0);
        }
      }

      m_powers.resize(m_levels.size(), std::vector<PowerFunc_t<Node>>(m_commands.size()));

      m_powers[m_levels["bank"]][m_commands["ACT"]] = Lambdas::Power::Bank::ACT<DDR5RVRR>;
      m_powers[m_levels["bank"]][m_commands["PRE"]] = Lambdas::Power::Bank::PRE<DDR5RVRR>;
      m_powers[m_levels["bank"]][m_commands["RD"]]  = Lambdas::Power::Bank::RD<DDR5RVRR>;
      m_powers[m_levels["bank"]][m_commands["WR"]]  = Lambdas::Power::Bank::WR<DDR5RVRR>;
      m_powers[m_levels["bank"]][m_commands["VRR"]]  = Lambdas::Power::Bank::VRR<DDR5RVRR>;
      m_powers[m_levels["bank"]][m_commands["RVRR"]]  = Lambdas::Power::Bank::RVRR<DDR5RVRR>;

      // m_powers[m_levels["rank"]][m_commands["REFsb"]] = Lambdas::Power::Rank::REFsb<DDR5RVRR>;
      // m_powers[m_levels["rank"]][m_commands["REFsb_end"]] = Lambdas::Power::Rank::REFsb_end<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["RFMsb"]] = Lambdas::Power::Rank::RFMsb<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["RFMsb_end"]] = Lambdas::Power::Rank::RFMsb_end<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["RRFMsb"]] = Lambdas::Power::Rank::RRFMsb<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["RRFMsb_end"]] = Lambdas::Power::Rank::RRFMsb_end<DDR5RVRR>;
      // m_powers[m_levels["rank"]][m_commands["DRFMsb"]] = Lambdas::Power::Rank::REFsb<DDR5RVRR>;
      // m_powers[m_levels["rank"]][m_commands["DRFMsb_end"]] = Lambdas::Power::Rank::REFsb_end<DDR5RVRR>;

      m_powers[m_levels["rank"]][m_commands["ACT"]] = Lambdas::Power::Rank::ACT<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["PRE"]] = Lambdas::Power::Rank::PRE<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["PREA"]] = Lambdas::Power::Rank::PREA<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["REFab"]] = Lambdas::Power::Rank::REFab<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["REFab_end"]] = Lambdas::Power::Rank::REFab_end<DDR5RVRR>;
      // m_powers[m_levels["rank"]][m_commands["RFMab"]] = Lambdas::Power::Rank::REFab<DDR5RVRR>;
      // m_powers[m_levels["rank"]][m_commands["RFMab_end"]] = Lambdas::Power::Rank::REFab_end<DDR5RVRR>;
      // m_powers[m_levels["rank"]][m_commands["DRFMab"]] = Lambdas::Power::Rank::REFab<DDR5RVRR>;
      // m_powers[m_levels["rank"]][m_commands["DRFMab_end"]] = Lambdas::Power::Rank::REFab_end<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["VRR"]] = Lambdas::Power::Rank::VRR<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["VRR_end"]] = Lambdas::Power::Rank::VRR_end<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["RVRR"]] = Lambdas::Power::Rank::VRR<DDR5RVRR>;
      m_powers[m_levels["rank"]][m_commands["RVRR_end"]] = Lambdas::Power::Rank::VRR_end<DDR5RVRR>;
      
      m_powers[m_levels["rank"]][m_commands["PREsb"]] = Lambdas::Power::Rank::PREsb<DDR5RVRR>;

      // register stats
      register_stat(s_total_background_energy).name("total_background_energy");
      register_stat(s_total_cmd_energy).name("total_cmd_energy");
      register_stat(s_total_energy).name("total_energy");
      register_stat(s_total_rfm_energy).name("total_rfm_energy");
      register_stat(s_total_rrfm_energy).name("total_rrfm_energy");
      register_stat(s_total_vrr_energy).name("total_vrr_energy");
      register_stat(s_total_rvrr_energy).name("total_rvrr_energy");
            
      for (auto& power_stat : m_power_stats){
        register_stat(power_stat.total_background_energy).name("total_background_energy_rank{}", power_stat.rank_id);
        register_stat(power_stat.total_cmd_energy).name("total_cmd_energy_rank{}", power_stat.rank_id);
        register_stat(power_stat.total_energy).name("total_energy_rank{}", power_stat.rank_id);
        register_stat(power_stat.act_background_energy).name("act_background_energy_rank{}", power_stat.rank_id);
        register_stat(power_stat.pre_background_energy).name("pre_background_energy_rank{}", power_stat.rank_id);
        register_stat(power_stat.active_cycles).name("active_cycles_rank{}", power_stat.rank_id);
        register_stat(power_stat.idle_cycles).name("idle_cycles_rank{}", power_stat.rank_id);
      }
    }

    void create_nodes() {
      int num_channels = m_organization.count[m_levels["channel"]];
      for (int i = 0; i < num_channels; i++) {
        Node* channel = new Node(this, nullptr, 0, i);
        m_channels.push_back(channel);
      }
    }
    
    void finalize() override {
      if (!m_drampower_enable)
        return;

      int num_channels = m_organization.count[m_levels["channel"]];
      int num_ranks = m_organization.count[m_levels["rank"]];
      for (int i = 0; i < num_channels; i++) {
        for (int j = 0; j < num_ranks; j++) {
          process_rank_energy(m_power_stats[i * num_ranks + j], m_channels[i]->m_child_nodes[j]);
        }
      }
    }

    void process_rank_energy(PowerStats& rank_stats, Node* rank_node) {
      
      Lambdas::Power::Rank::finalize_rank<DDR5RVRR>(rank_node, 0, AddrVec_t(), m_clk);

      size_t num_bankgroups = m_organization.count[m_levels["bankgroup"]];

      auto TS = [&](std::string_view timing) { return m_timing_vals(timing); };
      auto VE = [&](std::string_view voltage) { return m_voltage_vals(voltage); };
      auto CE = [&](std::string_view current) { return m_current_vals(current); };

      double tCK_ns = (double) TS("tCK_ps") / 1000.0;

      rank_stats.act_background_energy = (VE("VDD") * CE("IDD3N") + VE("VPP") * CE("IPP3N")) 
                                            * rank_stats.active_cycles * tCK_ns / 1E3;

      rank_stats.pre_background_energy = (VE("VDD") * CE("IDD2N") + VE("VPP") * CE("IPP2N")) 
                                            * rank_stats.idle_cycles * tCK_ns / 1E3;


      double act_cmd_energy  = (VE("VDD") * (CE("IDD0") - CE("IDD3N")) + VE("VPP") * (CE("IPP0") - CE("IPP3N"))) 
                                      * rank_stats.cmd_counters[m_cmds_counted("ACT")] * TS("nRAS") * tCK_ns / 1E3;

      double pre_cmd_energy  = (VE("VDD") * (CE("IDD0") - CE("IDD2N")) + VE("VPP") * (CE("IPP0") - CE("IPP2N"))) 
                                      * rank_stats.cmd_counters[m_cmds_counted("PRE")] * TS("nRP")  * tCK_ns / 1E3;

      double rd_cmd_energy   = (VE("VDD") * (CE("IDD4R") - CE("IDD3N")) + VE("VPP") * (CE("IPP4R") - CE("IPP3N"))) 
                                      * rank_stats.cmd_counters[m_cmds_counted("RD")] * TS("nBL") * tCK_ns / 1E3;

      double wr_cmd_energy   = (VE("VDD") * (CE("IDD4W") - CE("IDD3N")) + VE("VPP") * (CE("IPP4W") - CE("IPP3N"))) 
                                      * rank_stats.cmd_counters[m_cmds_counted("WR")] * TS("nBL") * tCK_ns / 1E3;

      double ref_cmd_energy  = (VE("VDD") * (CE("IDD5B")) + VE("VPP") * (CE("IPP5B"))) 
                                      * rank_stats.cmd_counters[m_cmds_counted("REF")] * TS("nRFC1") * tCK_ns / 1E3;

      double rfm_cmd_energy = (VE("VDD") * (CE("IDD0") - CE("IDD3N")) + VE("VPP") * (CE("IPP0") - CE("IPP3N"))) * num_bankgroups
                                      * rank_stats.cmd_counters[m_cmds_counted("RFM")] * TS("nRFMsb") * tCK_ns / 1E3;

      double rrfm_cmd_energy = (VE("VDD") * (CE("IDD0") - CE("IDD3N")) + VE("VPP") * (CE("IPP0") - CE("IPP3N"))) * num_bankgroups
                                      * rank_stats.cmd_counters[m_cmds_counted("RRFM")] * TS("nRRFMsb") * tCK_ns / 1E3;

      double vrr_cmd_energy  = (VE("VDD") * (CE("IDD0") - CE("IDD3N")) + VE("VPP") * (CE("IPP0") - CE("IPP3N"))) 
                                      * rank_stats.cmd_counters[m_cmds_counted("VRR")] * TS("nVRR") * tCK_ns / 1E3;

      double rvrr_cmd_energy  = (VE("VDD") * (CE("IDD0") - CE("IDD3N")) + VE("VPP") * (CE("IPP0")  - CE("IPP3N"))) 
                                      * rank_stats.cmd_counters[m_cmds_counted("RVRR")] * TS("nRVRR") * tCK_ns / 1E3;

      rank_stats.total_background_energy = rank_stats.act_background_energy + rank_stats.pre_background_energy;
      rank_stats.total_cmd_energy = act_cmd_energy 
                                    + pre_cmd_energy 
                                    + rd_cmd_energy
                                    + wr_cmd_energy 
                                    + ref_cmd_energy
                                    + rfm_cmd_energy
                                    + rrfm_cmd_energy
                                    + vrr_cmd_energy
                                    + rvrr_cmd_energy;

      rank_stats.total_energy = rank_stats.total_background_energy + rank_stats.total_cmd_energy;

      s_total_background_energy += rank_stats.total_background_energy;
      s_total_cmd_energy += rank_stats.total_cmd_energy;
      s_total_energy += rank_stats.total_energy;
      s_total_rfm_energy += rfm_cmd_energy;
      s_total_rrfm_energy += rrfm_cmd_energy;
      s_total_vrr_energy += vrr_cmd_energy;
      s_total_rvrr_energy += rvrr_cmd_energy;

      s_total_rfm_cycles[rank_stats.rank_id] = rank_stats.cmd_counters[m_cmds_counted("RFM")] * TS("nRFMsb");
      s_total_rrfm_cycles[rank_stats.rank_id] = rank_stats.cmd_counters[m_cmds_counted("RRFM")] * TS("nRRFMsb");
      s_total_vrr_cycles[rank_stats.rank_id] = rank_stats.cmd_counters[m_cmds_counted("VRR")] * TS("nVRR");
      s_total_rvrr_cycles[rank_stats.rank_id] = rank_stats.cmd_counters[m_cmds_counted("RVRR")] * TS("nRVRR");
    }
};


}        // namespace Ramulator
