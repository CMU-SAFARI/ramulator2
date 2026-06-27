#ifndef RAMULATOR_DRAM_COMMANDS_POPULATE_H
#define RAMULATOR_DRAM_COMMANDS_POPULATE_H

// No command includes here — each generated standard header (e.g., DDR4.h)
// includes its own command headers before including this file.
// DRAMSpec must be defined before this header is included.

#include <tuple>

namespace Ramulator {

// Forward reference — DRAMSpec is defined in dram_spec.h, included by each
// standard header before this file.
struct DRAMSpec;

template <class CmdImpl>
void register_command(DRAMSpec& spec, int cmd_id) {
  spec.command_meta[cmd_id] = CmdImpl::meta;
  spec.bank_targets[cmd_id] = CmdImpl::bank_target;
  if constexpr (requires { &CmdImpl::action; }) {
    spec.funcs.actions[cmd_id] = &CmdImpl::action;
  }
  if constexpr (requires { &CmdImpl::preq; }) {
    spec.funcs.preqs[cmd_id] = &CmdImpl::preq;
  }
  if constexpr (requires { &CmdImpl::rowhit; }) {
    spec.funcs.rowhits[cmd_id] = &CmdImpl::rowhit;
  }
  if constexpr (requires { &CmdImpl::rowopen; }) {
    spec.funcs.rowopens[cmd_id] = &CmdImpl::rowopen;
  }
}

template <class... Cmds>
void populate_commands(std::tuple<Cmds...>, DRAMSpec& spec) {
  spec.command_meta.resize(spec.command_count);
  spec.bank_targets.resize(spec.command_count);
  spec.funcs.resize(spec.command_count);
  int i = 0;
  (register_command<Cmds>(spec, i++), ...);
}

}  // namespace Ramulator

#endif  // RAMULATOR_DRAM_COMMANDS_POPULATE_H
