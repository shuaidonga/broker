#pragma once

#include <caf/behavior.hpp>
#include <caf/group.hpp>

#include "broker/atoms.hh"
#include "broker/data.hh"
#include "broker/detail/assert.hh"
#include "broker/detail/has_network_info.hh"
#include "broker/detail/lift.hh"
#include "broker/endpoint_info.hh"
#include "broker/error.hh"
#include "broker/logger.hh"
#include "broker/message.hh"
#include "broker/status.hh"
#include "broker/topic.hh"

namespace broker::mixin {

template <class Base, class Subtype>
class notifier : public Base {
public:
  using super = Base;

  using extended_base = notifier;

  using peer_id_type = typename super::peer_id_type;

  using communication_handle_type = typename Base::communication_handle_type;

  // The notifier embeds `endpoint_info` objects into status and error updates.
  // While we keep the implementation as generic as possible, the current
  // implementation `endpoint_info` prohibits any other peer ID type at the
  // moment.
  static_assert(std::is_same<peer_id_type, caf::node_id>::value);

  template <class... Ts>
  explicit notifier(Ts&&... xs) : super(std::forward<Ts>(xs)...) {
    // nop
  }

  void peer_discovered(const peer_id_type& peer_id) {
    BROKER_TRACE(BROKER_ARG(peer_id));
    emit(peer_id, sc_constant<sc::endpoint_discovered>(),
         "found a new peer in the network");
    super::peer_discovered(peer_id);
  }

  void peer_connected(const peer_id_type& peer_id,
                      const communication_handle_type& hdl) {
    BROKER_TRACE(BROKER_ARG(peer_id) << BROKER_ARG(hdl));
    emit(peer_id, sc_constant<sc::peer_added>(), "handshake successful");
    super::peer_connected(peer_id, hdl);
  }

  void peer_disconnected(const peer_id_type& peer_id,
                         const communication_handle_type& hdl,
                         const error& reason) {
    BROKER_TRACE(BROKER_ARG(peer_id) << BROKER_ARG(hdl) << BROKER_ARG(reason));
    // Calling emit() with the peer_id only trigges a network info lookup that
    // can stall this actor if we're already in shutdown mode. Hence, we perform
    // a manual cache lookup and simply omit the network information if we
    // cannot find a cached entry.
    network_info peer_addr;
    // Going through super here instead of using dref() makes sure that the
    // cache belongs to a mixin that runs its callback for `peer_disconnected`
    // later (potentially removing `hdl` from the cache).
    if (auto addr = super::cache().find(hdl))
      peer_addr = *addr;
    emit(peer_id, peer_addr, sc_constant<sc::peer_lost>(),
         "lost connection to remote peer");
    super::peer_disconnected(peer_id, hdl, reason);
  }

  void peer_removed(const peer_id_type& peer_id,
                    const communication_handle_type& hdl) {
    BROKER_TRACE(BROKER_ARG(peer_id) << BROKER_ARG(hdl));
    emit(peer_id, sc_constant<sc::peer_removed>(),
         "removed connection to remote peer");
    super::peer_removed(peer_id, hdl);
  }

  void peer_unreachable(const peer_id_type& peer_id) {
    BROKER_TRACE(BROKER_ARG(peer_id));
    emit(peer_id, sc_constant<sc::endpoint_unreachable>(),
         "lost the last path");
    super::peer_unreachable(peer_id);
  }

  void peer_unavailable(const network_info& addr) {
    BROKER_TRACE(BROKER_ARG(addr));
    auto self = super::self();
    emit({}, addr, ec_constant<ec::peer_unavailable>(),
         "unable to connect to remote peer");
  }

  template <class T>
  void cannot_remove_peer(const T& x) {
    BROKER_TRACE(BROKER_ARG(x));
    emit(x, ec_constant<ec::peer_invalid>(), "cannot unpeer from unknown peer");
    super::cannot_remove_peer(x);
  }

  void disable_notifications() {
    BROKER_TRACE("");
    disable_notifications_ = true;
  }

  template <class... Fs>
  caf::behavior make_behavior(Fs... fs) {
    using detail::lift;
    auto& d = dref();
    return super::make_behavior(
      fs..., lift<atom::no_events>(d, &Subtype::disable_notifications),
      [=](atom::publish, endpoint_info& receiver, data_message& msg) {
        dref().ship(msg, receiver.node);
      },
      [](atom::add, atom::status, const caf::actor&) {
        // TODO: this handler exists only for backwards-compatibility. It used
        //       to register status subscribers for synchronization. Eventually,
        //       we should either re-implement the synchronization if necessary
        //       or remove this handler.
      });
  }

private:
  auto& dref() {
    return *static_cast<Subtype*>(this);
  }

  void emit(const status& stat) {
    auto dmsg = make_data_message(topics::statuses, get_as<data>(stat));
    dref().ship_locally(std::move(dmsg));
  }

  void emit(const error& err) {
    auto dmsg = make_data_message(topics::errors, get_as<data>(err));
    dref().ship_locally(std::move(dmsg));
  }

  template <class Enum, Enum Code>
  void emit(const peer_id_type& peer_id, const network_info& x,
            std::integral_constant<Enum, Code>, const char* msg) {
    BROKER_INFO("emit:" << Code << x);
    if (disable_notifications_)
      return;
    if constexpr (std::is_same<Enum, sc>::value)
      emit(status::make<Code>(endpoint_info{peer_id, x}, msg));
    else
      emit(make_error(Code, endpoint_info{peer_id, x}, msg));
  }

  template <class Enum, Enum Code>
  void emit(const network_info& x, std::integral_constant<Enum, Code>,
            const char* msg) {
    BROKER_INFO("emit:" << Code << x);
    if (disable_notifications_)
      return;
    if constexpr (std::is_same<Enum, sc>::value) {
      emit(status::make<Code>(endpoint_info{{}, x}, msg));
    } else {
      emit(make_error(Code, endpoint_info{{}, x}, msg));
    }
  }

  /// Reports an error to all status subscribers.
  template <class EnumConstant>
  void emit(const communication_handle_type& hdl, EnumConstant code,
            const char* msg) {
    static_assert(detail::has_network_info_v<EnumConstant>);
    if (disable_notifications_)
      return;
    auto unbox_or_default = [](auto maybe_value) {
      if (maybe_value)
        return std::move(*maybe_value);
      return typename decltype(maybe_value)::value_type{};
    };
    emit(unbox_or_default(get_peer_id(dref().tbl(), hdl)),
         unbox_or_default(dref().cache().find(hdl)), code, msg);
  }

  template <class EnumConstant>
  void emit(const peer_id_type& peer_id, EnumConstant code, const char* msg) {
    BROKER_TRACE(BROKER_ARG(peer_id) << BROKER_ARG2("code", EnumConstant::value)
                                     << BROKER_ARG(msg));
    if (disable_notifications_)
      return;
    using value_type = typename EnumConstant::value_type;
    if constexpr (detail::has_network_info_v<EnumConstant>) {
      network_info net;
      auto& tbl = dref().tbl();
      if (auto i = tbl.find(peer_id); i != tbl.end() && i->second.hdl)
        if (auto maybe_net = dref().cache().find(i->second.hdl))
          net = std::move(*maybe_net);
       emit(peer_id, net, code, msg);
    } else if constexpr (std::is_same<value_type, sc>::value) {
      emit(status::make<EnumConstant::value>(peer_id, msg));
    } else {
      emit(make_error(EnumConstant::value, endpoint_info{peer_id, nil}, msg));
    }
  }

  bool disable_notifications_ = false;
};

} // namespace broker::mixin
