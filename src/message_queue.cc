#include <caf/send.hpp>

#include "broker/atoms.hh"
#include "broker/message_queue.hh"

namespace broker {

message_queue::message_queue() = default;

message_queue::message_queue(topic prefix, const endpoint& e)
  : subscription_prefix_{std::move(prefix)} {
  caf::anon_send(*static_cast<caf::actor*>(e.handle()),
                 local_sub_atom::value, subscription_prefix_,
                 *static_cast<caf::actor*>(this->handle()));
}

const topic& message_queue::get_topic_prefix() const {
  return subscription_prefix_;
}

} // namespace broker

// Begin C API
#include "broker/broker.h"
using std::nothrow;

void broker_deque_of_message_delete(broker_deque_of_message* d) {
  delete reinterpret_cast<std::deque<broker::message>*>(d);
}

size_t broker_deque_of_message_size(const broker_deque_of_message* d) {
  auto dd = reinterpret_cast<const std::deque<broker::message>*>(d);
  return dd->size();
}

broker_message* broker_deque_of_message_at(broker_deque_of_message* d,
                                           size_t idx) {
  auto dd = reinterpret_cast<std::deque<broker::message>*>(d);
  return reinterpret_cast<broker_message*>(&(*dd)[idx]);
}

void broker_deque_of_message_erase(broker_deque_of_message* d, size_t idx) {
  auto dd = reinterpret_cast<std::deque<broker::message>*>(d);
  dd->erase(dd->begin() + idx);
}

broker_message_queue*
broker_message_queue_create(const broker_string* topic_prefix,
                            const broker_endpoint* e) {
  auto ee = reinterpret_cast<const broker::endpoint*>(e);
  auto topic = reinterpret_cast<const std::string*>(topic_prefix);
  auto rval = new (nothrow) broker::message_queue(*topic, *ee);
  return reinterpret_cast<broker_message_queue*>(rval);
}

const broker_string*
broker_message_queue_topic_prefix(const broker_message_queue* q) {
  auto qq = reinterpret_cast<const broker::message_queue*>(q);
  return reinterpret_cast<const broker_string*>(&qq->get_topic_prefix());
}

int broker_message_queue_fd(const broker_message_queue* q) {
  auto qq = reinterpret_cast<const broker::message_queue*>(q);
  return qq->fd();
}

broker_deque_of_message*
broker_message_queue_want_pop(const broker_message_queue* q) {
  auto rval = new (nothrow) std::deque<broker::message>;
  if (!rval)
    return nullptr;
  auto qq = reinterpret_cast<const broker::message_queue*>(q);
  *rval = qq->want_pop();
  return reinterpret_cast<broker_deque_of_message*>(rval);
}

broker_deque_of_message*
broker_message_queue_need_pop(const broker_message_queue* q) {
  auto rval = new (nothrow) std::deque<broker::message>;
  if (!rval)
    return nullptr;
  auto qq = reinterpret_cast<const broker::message_queue*>(q);
  *rval = qq->need_pop();
  return reinterpret_cast<broker_deque_of_message*>(rval);
}
