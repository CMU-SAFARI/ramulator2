#ifndef     RAMULATOR_BASE_SERIALIZATION_H
#define     RAMULATOR_BASE_SERIALIZATION_H

#include <string>


namespace Ramulator {

/**
 * @brief    Abstract base class for serializable objects in Ramulator.
 * 
 */
template<class T>
class Serializable {
  friend T;

  public:
    /**
     * @brief Saves the desired objects to a file.
     * 
     */
    virtual void serialize() = 0;

    /**
     * @brief Loads the desired objects from a file.
     * 
     */
    virtual void deserialize() = 0;
};  


}        // namespace Ramulator


#endif   // RAMULATOR_BASE_SERIALIZATION_H