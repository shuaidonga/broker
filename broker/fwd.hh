#ifndef BROKER_FWD_HH
#define BROKER_FWD_HH

#include <cstdint>

namespace broker {

class context;
class endpoint;
class blocking_endpoint;
class nonblocking_endpoint;

struct network_info;

using endpoint_id = uint64_t;

// Arithmetic data types
using boolean = bool;
using count = uint64_t;
using integer = int64_t;
using real = double;

namespace store {

class backend;
class clone;
class frontend;
class master;

} // namespace store

namespace detail {

class flare_actor;
class mailbox;

} // namespace detail

} // namespace broker

#endif // BROKER_FWD_HH
