#ifndef BROKER_DETAIL_CORE_ACTOR_HH
#define BROKER_DETAIL_CORE_ACTOR_HH

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <caf/actor.hpp>
#include <caf/event_based_actor.hpp>
#include <caf/stateful_actor.hpp>

#include "broker/endpoint_info.hh"
#include "broker/error.hh"
#include "broker/network_info.hh"
#include "broker/optional.hh"
#include "broker/peer_info.hh"
#include "broker/status.hh"

#include "broker/detail/network_cache.hh"
#include "broker/detail/radix_tree.hh"
#include "broker/detail/stream_governor.hh"
#include "broker/detail/stream_relay.hh"

namespace broker {
namespace detail {

struct core_state {
  // --- nested types ----------------------------------------------------------

  struct pending_peer_state {
    caf::stream_id sid;
    caf::response_promise rp;
  };

  using pending_peers_map = std::unordered_map<caf::actor, pending_peer_state>;

  /// Identifies the two individual streams forming a bidirectional channel.
  /// The first ID denotes the *input*  and the second ID denotes the *output*.
  using stream_id_pair = std::pair<caf::stream_id, caf::stream_id>;

  // --- construction ----------------------------------------------------------
  
  core_state(caf::event_based_actor* ptr);

  /// Establishes all invariants.
  void init(filter_type initial_filter);

  // --- message introspection -------------------------------------------------

  /// Returns the peer that sent the current message.
  /// @pre `xs.match_elements<stream_msg>()`
  caf::strong_actor_ptr prev_peer_from_handshake();

  // --- filter management -----------------------------------------------------

  /// Sends the current filter to all peers.
  void update_filter_on_peers();

  /// Adds `xs` to our filter and update all peers on changes.
  void add_to_filter(filter_type xs);

  // --- convenience factory functions for querying state ----------------------

  caf::stream_handler_ptr make_relay(const caf::stream_id& sid) const;

  // --- convenience functions for querying state ------------------------------

  /// Returns whether `x` is either a pending peer or a connected peer.
  bool has_peer(const caf::actor& x);

  /// Returns whether a master for `name` probably exists already on one of our
  /// peers.
  bool has_remote_master(const std::string& name);

  // --- convenience functions for sending errors and events -------------------

  template <ec ErrorCode>
  void emit_error(caf::actor hdl, const char* msg) {
    auto emit = [=](network_info x) {
      self->send(
        statuses_, atom::local::value,
        make_error(ErrorCode, endpoint_info{hdl.node(), std::move(x)}, msg));
    };
    if (self->node() != hdl.node())
      cache.fetch(hdl,
                  [=](network_info x) { emit(std::move(x)); },
                  [=](caf::error) { emit({}); });
    else
      emit({});
  }

  template <ec ErrorCode>
  void emit_error(caf::strong_actor_ptr hdl, const char* msg) {
    emit_error<ErrorCode>(caf::actor_cast<caf::actor>(hdl), msg);
  }

  template <ec ErrorCode>
  void emit_error(network_info inf, const char* msg) {
    auto emit = [=](caf::actor x) {
      self->send(
        statuses_, atom::local::value,
        make_error(ErrorCode, endpoint_info{x.node(), inf}, msg));
    };
    cache.fetch(inf,
                [=](caf::actor x) { emit(std::move(x)); },
                [=](caf::error) { emit({}); });
  }

  template <sc StatusCode>
  void emit_status(caf::actor hdl, const char* msg) {
    auto emit = [=](network_info x) {
      self->send(statuses_, atom::local::value,
                 status::make<StatusCode>(
                 endpoint_info{hdl.node(), std::move(x)}, msg));
    };
    if (self->node() != hdl.node())
      cache.fetch(hdl,
                  [=](network_info x) { emit(x); },
                  [=](caf::error) { emit({}); });
    else
      emit({});
  }

  template <sc StatusCode>
  void emit_status(caf::strong_actor_ptr hdl, const char* msg) {
    emit_status<StatusCode>(caf::actor_cast<caf::actor>(std::move(hdl)), msg);
  }

  // --- member variables ------------------------------------------------------

  /// Stores all master actors created by this core.
  std::unordered_map<std::string, caf::actor> masters;

  /// Stores all clone actors created by this core.
  std::unordered_multimap<std::string, caf::actor> clones;

  /// Requested topics on this core.
  filter_type filter;
 
  /// Multiplexes local streams and streams for peers.
  detail::stream_governor_ptr governor;

  /// Maps pending peer handles to output IDs. An invalid stream ID indicates
  /// that only "step #0" was performed so far. An invalid stream ID
  /// corresponds to `peer_status::connecting` and a valid stream ID
  /// cooresponds to `peer_status::connected`. The status for a given handle
  /// `x` is `peer_status::peered` if `governor->has_peer(x)` returns true.
  pending_peers_map pending_peers;

  /// Points to the owning actor.
  caf::event_based_actor* self;

  /// Connects the governor to the input of local actor.
  caf::stream_handler_ptr worker_relay;

  /// Connects the governor to the input of local actor.
  caf::stream_handler_ptr store_relay;

  /// Associates network addresses to remote actor handles and vice versa.
  network_cache cache;

  /// Caches the CAF group for error messages.
  caf::group errors_;

  /// Caches the CAF group for status messages.
  caf::group statuses_;

  /// Name shown in logs for all instances of this actor.
  static const char* name;

  /// Set to `true` after receiving a shutdown message from the endpoint.
  bool shutting_down;

  /// Stores which stream sources are local actors. Storing the actor handle is
  /// sufficient, because we assign the same stream ID to all of our sources.
  std::unordered_map<caf::stream_id, caf::strong_actor_ptr> local_sources;
};

caf::behavior core_actor(caf::stateful_actor<core_state>* self,
                         filter_type initial_filter);

} // namespace detail
} // namespace broker

#endif // BROKER_DETAIL_CORE_ACTOR_HH
