#ifndef RAMULATOR_CONTROLLER_SMD_I_SMD_MECHANISM_H
#define RAMULATOR_CONTROLLER_SMD_I_SMD_MECHANISM_H

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/controller/smd/maintenance.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

struct SMDContext {
    const DRAMSpec* spec = nullptr;

    int num_ranks = 1;
    int banks_per_rank = 1;
    int num_sas = 1;
    int rows_per_sa = 1;
    int tCK_ps = 0;

    int chips_per_rank = 1;
    int num_chips = 1;

    std::vector<SMD::LockTable*> locks;
    SMD::RankEngine::ActiveFn is_sa_active;

    int auto_latency() const {
        const double rows_per_bank = static_cast<double>(num_sas) * rows_per_sa;
        return static_cast<int>(std::ceil(spec->get_timing_value("nRFC") * 8192.0 / rows_per_bank));
    }

    int resolve_latency(int configured) const {
        return configured > 0 ? configured : auto_latency();
    }
};

using SMDStatMap = std::map<std::string, uint64_t>;

class ISMDMechanism {
    RAMULATOR_REGISTER_INTERFACE(ISMDMechanism, "smd_mechanism")
public:
    virtual bool needs_chip_simulation() const { return false; }
    virtual void attach(SMDContext& ctx) = 0;
    virtual void tick() {}
    virtual void on_activation(int bank, int sa, int row) {}
    virtual int64_t row_open_limit() const { return 0; }
    virtual void collect_smd_stats(SMDStatMap& out) const {}
    virtual void reset_smd_stats() {}
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_SMD_I_SMD_MECHANISM_H