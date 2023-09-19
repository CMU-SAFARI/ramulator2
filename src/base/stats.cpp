#include "base/stats.h"

namespace Ramulator {

YAML::Emitter& operator << (YAML::Emitter& emitter, const Stats& s) {
  for (auto [stat_name, stat_ptr] : s._registry) {
    stat_ptr->emit_to(emitter);
  }
	return emitter;
}

}        // namespace Ramulator
