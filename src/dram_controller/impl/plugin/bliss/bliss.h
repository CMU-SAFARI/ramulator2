#ifndef RAMULATOR_PLUGIN_BLISS_H_
#define RAMULATOR_PLUGIN_BLISS_H_

namespace Ramulator {

class IBLISS {
public:
    virtual bool is_blacklisted(int source_id) = 0;
};

}

#endif  // RAMULATOR_PLUGIN_BLISS_H_ 