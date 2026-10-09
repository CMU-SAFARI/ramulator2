#ifndef RAMULATOR_POWER_I_POWER_MODEL_H
#define RAMULATOR_POWER_I_POWER_MODEL_H

namespace Ramulator {

// Query interface for components that react to DRAM power (e.g., a power-aware scheduler).
// Energies cover the current stats window (since the last reset_stats()) and are evaluated at the
// controller's current clock. Commands are applied at the end of the tick they complete on, so a
// query made during a tick does not yet include the commands completing in that tick.
class IPowerModel {
 public:
  virtual ~IPowerModel() = default;

  // Energy of the whole channel, in pJ.
  virtual double energy_pJ() = 0;

  // Energy of one power domain (an HBM pseudo-channel), in pJ.
  virtual double domain_energy_pJ(int domain_id) = 0;
};

}  // namespace Ramulator

#endif  // RAMULATOR_POWER_I_POWER_MODEL_H
