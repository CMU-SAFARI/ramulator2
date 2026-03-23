#ifndef RAMULATOR_TRANSLATION_I_TRANSLATION_H
#define RAMULATOR_TRANSLATION_I_TRANSLATION_H

#include "ramulator/base/base.h"
#include "ramulator/base/request.h"

namespace Ramulator {

// Translates virtual addresses to physical addresses before memory access.
class ITranslation {
  RAMULATOR_REGISTER_INTERFACE(ITranslation, "translation")
 public:
  // Translates req.addr in-place. Returns false if translation is not yet ready.
  virtual bool translate(Request& req) = 0;
};

}  // namespace Ramulator

#endif  // RAMULATOR_TRANSLATION_I_TRANSLATION_H
