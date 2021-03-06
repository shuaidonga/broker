#pragma once

#include <fstream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <caf/actor.hpp>
#include <caf/event_based_actor.hpp>
#include <caf/extend.hpp>
#include <caf/stateful_actor.hpp>

#include "broker/alm/stream_transport.hh"
#include "broker/atoms.hh"
#include "broker/configuration.hh"
#include "broker/detail/network_cache.hh"
#include "broker/detail/radix_tree.hh"
#include "broker/endpoint.hh"
#include "broker/endpoint_info.hh"
#include "broker/error.hh"
#include "broker/filter_type.hh"
#include "broker/logger.hh"
#include "broker/mixin/connector.hh"
#include "broker/mixin/data_store_manager.hh"
#include "broker/mixin/notifier.hh"
#include "broker/mixin/recorder.hh"
#include "broker/network_info.hh"
#include "broker/optional.hh"
#include "broker/peer_info.hh"
#include "broker/status.hh"

namespace broker {

class core_manager
  : public caf::extend<alm::stream_transport<core_manager, caf::node_id>,
                       core_manager>:: //
    with<mixin::connector, mixin::notifier, mixin::data_store_manager,
         mixin::recorder> {
public:
  // --- member types ----------------------------------------------------------

  using super = extended_base;

  /// Identifies the two individual streams forming a bidirectional channel.
  /// The first ID denotes the *input*  and the second ID denotes the
  /// *output*.
  using stream_id_pair = std::pair<caf::stream_slot, caf::stream_slot>;

  // --- construction ----------------------------------------------------------

  core_manager(caf::event_based_actor* ptr, const filter_type& filter,
               broker_options opts, endpoint::clock* ep_clock);

  // --- initialization --------------------------------------------------------

  caf::behavior make_behavior();

  // --- properties ------------------------------------------------------------

  const auto& filter() const {
    return filter_;
  }

  const auto& options() const {
    return options_;
  }

  bool shutting_down() const {
    return shutting_down_;
  }

  // --- filter management -----------------------------------------------------

  /// Sends the current filter to all peers.
  void update_filter_on_peers();

  /// Adds `xs` to our filter and update all peers on changes.
  void subscribe(filter_type xs);

  // --- convenience functions for querying state ------------------------------

  /// Returns whether `x` is either a pending peer or a connected peer.
  bool has_peer(const caf::actor& x);

  /// Returns whether a master for `name` probably exists already on one of
  /// our peers.
  bool has_remote_subscriber(const topic& x) noexcept;

  // --- callbacks -------------------------------------------------------------
  //
  void peer_connected(const peer_id_type& peer_id,
                      const communication_handle_type& hdl);

  void sync_with_status_subscribers(caf::actor new_peer);

private:
  // --- member variables ------------------------------------------------------

  /// A copy of the current Broker configuration options.
  broker_options options_;

  /// Requested topics on this core.
  filter_type filter_;

  /// Set to `true` after receiving a shutdown message from the endpoint.
  bool shutting_down_ = false;

  /// Keeps track of all actors that subscribed to status updates.
  std::unordered_set<caf::actor> status_subscribers_;

  /// Keeps track of all actors that currently wait for handshakes to
  /// complete.
  std::unordered_map<caf::actor, size_t> peers_awaiting_status_sync_;
};

struct core_state {
  /// Establishes all invariants.
  void init(filter_type initial_filter, broker_options opts,
            endpoint::clock* ep_clock);

  /// Multiplexes local streams and streams for peers.
  caf::intrusive_ptr<core_manager> mgr;

  /// Gives this actor a recognizable name in log output.
  static inline const char* name = "core";
};

using core_actor_type = caf::stateful_actor<core_state>;

caf::behavior core_actor(core_actor_type* self, filter_type initial_filter,
                         broker_options opts, endpoint::clock* clock);

} // namespace broker
