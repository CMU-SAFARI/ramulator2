#ifndef RAMULATOR_DRAM_DRAM_H
#define RAMULATOR_DRAM_DRAM_H

#include <string>
#include <vector>
#include <map>
#include <functional>

#include "base/base.h"
#include "dram/spec.h"
#include "dram/node.h"

namespace Ramulator {

class IDRAM : public Clocked<IDRAM> {
  RAMULATOR_REGISTER_INTERFACE(IDRAM, "DRAM", "DRAM Device Model Interface")

  /************************************************
   *                Organization
   ***********************************************/   
  public:
    int m_internal_prefetch_size = -1;  // Internal prefetch (xn) size: How many columns are fetched into the I/O? e.g., DDR4 has 8n prefetch.
    SpecDef m_levels;                   // Definition (i.e., names and ids) of the levels in the hierarchy
    Organization m_organization;        // The organization of the device (density, dq, levels)
    int m_channel_width = -1;           // Channel width (should be set by the implementation's config)


  /************************************************
   *             Requests & Commands
   ***********************************************/
  public:
    SpecDef m_commands;                                   // The definition of all DRAM commands
    SpecLUT<Level_t> m_command_scopes{m_commands};        // A LUT of the scopes (i.e., at which organization level) of the DRAM commands
    SpecLUT<DRAMCommandMeta> m_command_meta{m_commands};  // A LUT to check which DRAM command opens a row 

    SpecDef m_requests;                                     // The definition of all requests supported
    SpecLUT<Command_t> m_request_translations{m_requests};  // A LUT of the final DRAM commands needed by every request

    // TODO: make this a priority queue
    std::vector<FutureAction> m_future_actions;  // A vector of requests that requires future state changes

  /************************************************
   *                Node States
   ***********************************************/
  public:
    SpecDef m_states;
    SpecLUT<State_t> m_init_states{m_states};


  /************************************************
   *                   Timing
   ***********************************************/
  public:
    SpecDef m_timings;                      // The names of the timing constraints
    SpecLUT<int> m_timing_vals{m_timings};  // The LUT of the values for each timing constraints

    TimingCons m_timing_cons;           // The actual timing constraints used by Ramulator's DRAM model

    Clk_t m_read_latency = -1;          // Number of cycles needed between issuing RD command and receiving data.

  /***********************************************
   *                   Power
   ***********************************************/
  public:
    bool m_drampower_enable = false;             // Whether to enable DRAM power model

    std::vector<PowerStats> m_power_stats;      // The power stats and counters PER channel PER rank (ch0rank0, ch0rank1... ch1rank0,...)
    SpecDef m_voltages;                         // The names of the voltage constraints
    SpecLUT<double> m_voltage_vals{m_voltages}; // The LUT of the values for each voltage constraints
    SpecDef m_currents;                         // The names of the current constraints
    SpecLUT<double> m_current_vals{m_currents}; // The LUT of the values for each current constraints
    SpecDef m_cmds_counted;

    bool m_power_debug = false;

    double s_total_background_energy = 0; // Total background energy consumed by the device
    double s_total_cmd_energy = 0;        // Total command energy consumed by the device
    double s_total_energy = 0;            // Total energy consumed by the device

  /************************************************
   *          Device Behavior Interface
   ***********************************************/   
  public:
    /**
     * @brief   Issues a command with its address to the device.
     * @details
     * Issues the given command with its address to the device. This function should then update
     * the states of involved nodes in the device hierarchy and their timing information.
     * 
     */
    virtual void issue_command(int command, const AddrVec_t& addr_vec) = 0;

    /**
     * @brief    Returns the prequisite command of the given command and address
     * @details  
     * Given a command and its address, this function should return the prequisite 
     * command based on the current state of the device.
     * 
     */
    virtual int get_preq_command(int command, const AddrVec_t& addr_vec) = 0;

    /**
     * @brief     Checks whether the device is ready to accept the given command.
     * @details
     * Given a command, its address, and the current clock cycle, this function should return 
     * whether the current state of the device allows execution of the command.
     * 
     */
    virtual bool check_ready(int command, const AddrVec_t& addr_vec) = 0;

    /**
     * @brief     Checks whether the command will result in a rowbuffer hit
     * @details
     * Given a command and its address, this function should return whether it will
     * hit in an opened rowbuffer.
     * 
     */
    virtual bool check_rowbuffer_hit(int command, const AddrVec_t& addr_vec) = 0;

    /**
     * @brief     
     * @details
     */
    virtual bool check_node_open(int command, const AddrVec_t& addr_vec) = 0;

    /**
     * @brief     An universal interface for the host to change DRAM configurations on the fly
     * @details
     * Provide a universal interface to let the host change the DRAM configurations on the fly (e.g., set refresh mode),
     * given that the host knows what it can configure with the DRAM. This is slow so should NOT be called too often
     * TODO: Alternatively, we can keep adding new functionalities to this DRAM interface...
     * 
     */
    virtual void notify(std::string_view key, uint64_t value) {};

    /**
     * @brief     
    */
    virtual void finalize() {};

  /************************************************
   *        Interface to Query Device Spec
   ***********************************************/   
  public:
    int get_level_size(std::string name) {
      try {
        int level_idx = m_levels(name);
        return m_organization.count[level_idx];
      } catch (const std::out_of_range& e) {
        return -1;
      }
    }
};

#define RAMULATOR_DECLARE_SPECS() \
  IDRAM::m_internal_prefetch_size = m_internal_prefetch_size; \
  IDRAM::m_levels = m_levels; \
  IDRAM::m_commands = m_commands; \
  IDRAM::m_command_scopes = m_command_scopes; \
  IDRAM::m_command_meta = m_command_meta; \
  IDRAM::m_requests = m_requests; \
  IDRAM::m_request_translations = m_request_translations; \
  IDRAM::m_states = m_states; \
  IDRAM::m_init_states = m_init_states; \
  IDRAM::m_timings = m_timings; \
  IDRAM::m_voltages = m_voltages; \
  IDRAM::m_currents = m_currents; \

}        // namespace Ramulator

#endif   // RAMULATOR_DRAM_DRAM_H

