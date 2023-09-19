#ifndef     RAMULATOR_FRONTEND_TRANSLATION_H
#define     RAMULATOR_FRONTEND_TRANSLATION_H

#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

#include "base/base.h"
#include "base/request.h"


namespace Ramulator {

DECLARE_DEBUG_FLAG(DTRANSLATE);
// ENABLE_DEBUG_FLAG(DTRANSLATE);

class ITranslation {
  RAMULATOR_REGISTER_INTERFACE(ITranslation, "Translation", "Interface for translation virtual address to physical address.")   
  public:
    /**
     * @brief    Performs address translation for the request req.
     * 
     */
    virtual bool translate(Request& req) = 0;

    /**
     * @brief    Reserves addr for the purpose indicated by type
     * 
     */
    virtual bool reserve(const std::string& type, Addr_t addr) {
      return false;
    };

    /**
     * @brief    Returns the maximum physical address
     * 
     */
    virtual Addr_t get_max_addr() {
      return 0;
    };

};

}        // namespace Ramulator


#endif   // RAMULATOR_FRONTEND_TRANSLATION_H