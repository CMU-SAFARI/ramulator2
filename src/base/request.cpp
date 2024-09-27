#include "base/request.h"

namespace Ramulator {

Request::Request(Addr_t addr, int type): addr(addr), type_id(type) {};

Request::Request(AddrVec_t addr_vec, int type): addr_vec(addr_vec), type_id(type) {};

Request::Request(Addr_t addr, int type, int source_id, std::function<void(Request&)> callback):
addr(addr), type_id(type), source_id(source_id), callback(callback) {};

Request::Request(Addr_t addr, int type, uint8_t* payload, int payload_size): addr(addr), type_id(type), payload(payload),  payload_size(payload_size) {};

Request::Request(Addr_t addr, int type, int source_id, uint8_t* payload, int payload_size, std::function<void(Request&)> callback):
addr(addr), type_id(type), source_id(source_id), payload(payload), payload_size(payload_size), callback(callback) {};

}        // namespace Ramulator

