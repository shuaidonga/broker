#include "broker/detail/stream_governor.hh"

#include <caf/event_based_actor.hpp>
#include <caf/message.hpp>
#include <caf/stream.hpp>
#include <caf/upstream_path.hpp>

#include <caf/policy/broadcast.hpp>
#include <caf/policy/greedy.hpp>

#include "broker/detail/core_actor.hh"
#include "broker/detail/stream_relay.hh"

namespace broker {
namespace detail {

namespace {

caf::message generic_factory(const caf::stream_id& x) {
  return caf::make_message(caf::stream<caf::message>{x});
}

} // namespace <anonymous>

// --- nested types ------------------------------------------------------------

stream_governor::peer_data::peer_data(stream_governor* parent, filter_type y,
                                      const caf::stream_id& downstream_sid)
  : filter(std::move(y)),
    out(parent->state()->self, downstream_sid),
    relay(caf::make_counted<stream_relay>(parent, downstream_sid,
                                          generic_factory)) {
  // nop
}

stream_governor::peer_data::~peer_data() {
  // nop
}

void stream_governor::peer_data::send_stream_handshake() {
  CAF_LOG_TRACE("");
  auto self = static_cast<caf::scheduled_actor*>(out.self());
  caf::stream<caf::message> token{out.sid()};
  auto data = caf::make_message(token, atom::ok::value,
                                caf::actor_cast<caf::actor>(self->ctrl()));
  remote_core->enqueue(caf::make_mailbox_element(
                         self->ctrl(), caf::message_id::make(), {self->ctrl()},
                         caf::make<caf::stream_msg::open>(
                           token.id(), std::move(data), self->ctrl(), hdl(),
                           caf::stream_priority::normal, false)),
                       self->context());
  self->streams().emplace(out.sid(), relay);
}

const caf::strong_actor_ptr& stream_governor::peer_data::hdl() const {
  auto& l = out.paths();
  CAF_ASSERT(l.size() == 1);
  return l.front()->hdl;
}

// --- constructors and destructors --------------------------------------------

stream_governor::stream_governor(core_state* state)
  : state_(state),
    in_(state->self),
    workers_(state->self, state->self->make_stream_id()),
    stores_(state->self, state->self->make_stream_id()) {
  CAF_LOG_DEBUG("started governor with workers SID"
                << workers_.sid() << "and stores SID" << stores_.sid());
}

stream_governor::peer_data*
stream_governor::peer(const caf::actor& remote_core) {
  auto i = peers_.find(remote_core);
  return i != peers_.end() ? i->second.get() : nullptr;
}

stream_governor::~stream_governor() {
  // nop
}

stream_governor::peer_data*
stream_governor::add_peer(caf::strong_actor_ptr downstream_handle,
                          caf::actor remote_core, const caf::stream_id& sid,
                          filter_type filter) {
  CAF_LOG_TRACE(CAF_ARG(downstream_handle)
                << CAF_ARG(remote_core) << CAF_ARG(sid) << CAF_ARG(filter));
  auto ptr = caf::make_counted<peer_data>(this, std::move(filter), sid);
  ptr->out.add_path(downstream_handle);
  ptr->remote_core = remote_core;
  auto res = peers_.emplace(std::move(remote_core), ptr);
  if (res.second) {
    auto self = static_cast<caf::scheduled_actor*>(ptr->out.self());
    self->streams().emplace(sid, ptr->relay);
    input_to_peers_.emplace(sid, ptr);
    return ptr.get(); // safe, because two more refs to ptr exist
  }
  return nullptr;
}

bool stream_governor::remove_peer(const caf::actor& hdl) {
  CAF_LOG_TRACE(CAF_ARG(hdl));
  auto i = peers_.find(hdl);
  if (i == peers_.end())
    return false;
  auto self = state_->self;
  auto sptr = caf::actor_cast<caf::strong_actor_ptr>(self);
  caf::error err = caf::exit_reason::user_shutdown;
  auto& pd = *i->second;
  self->streams().erase(pd.incoming_sid);
  self->streams().erase(pd.out.sid());
  pd.out.abort(sptr, err);
  peers_.erase(i);
  return true;
}

bool stream_governor::update_peer(const caf::actor& hdl, filter_type filter) {
  CAF_LOG_TRACE(CAF_ARG(hdl) << CAF_ARG(filter));
  auto i = peers_.find(hdl);
  if (i == peers_.end()) {
    CAF_LOG_DEBUG("cannot update filter on unknown peer");
    return false;
  }
  i->second->filter = std::move(filter);
  return true;
}

caf::error stream_governor::add_downstream(const caf::stream_id& sid,
                                           caf::strong_actor_ptr&) {
  CAF_LOG_ERROR("add_downstream on governor called");
  return caf::sec::invalid_stream_state;
}

void stream_governor::local_push(worker_element&& x) {
  workers_.push(std::move(x));
  workers_.emit_batches();
}

void stream_governor::local_push(topic&& t, data&& x) {
  worker_element e{std::move(t), std::move(x)};
  local_push(std::move(e));
}

void stream_governor::push(topic&& t, data&& x) {
  CAF_LOG_TRACE(CAF_ARG(t) << CAF_ARG(x));
  auto selected = [](const filter_type& f, const worker_element& e) -> bool {
    for (auto& key : f)
      if (key == e.first)
        return true;
    return false;
  };
  worker_element e{std::move(t), std::move(x)};
  for (auto& kvp : peers_) {
    auto& out = kvp.second->out;
    if (selected(kvp.second->filter, e)) {
      out.push(caf::make_message(e.first, e.second));
      out.emit_batches();
    }
  }
  local_push(std::move(e));
}

void stream_governor::push(topic&& t, internal_command&& x) {
  CAF_LOG_TRACE(CAF_ARG(t) << CAF_ARG(x));
  auto selected = [](const filter_type& f, const store_element& e) -> bool {
    for (auto& key : f)
      if (key == e.first)
        return true;
    return false;
  };
  CAF_LOG_DEBUG("push internal to command to" << peers_.size() << "peers and"
                << stores_.paths().size() << "data stores");
  store_element e{std::move(t), std::move(x)};
  for (auto& kvp : peers_) {
    auto& out = kvp.second->out;
    if (selected(kvp.second->filter, e)) {
      out.push(caf::make_message(e.first, e.second));
      out.emit_batches();
    }
  }
  stores_.push(std::move(e));
  stores_.emit_batches();
}

void stream_governor::downstream_demand(caf::downstream_path* path,
                                        long demand) {
  path->open_credit += demand;
  push();
  assign_credit();
}

namespace {

template <class T>
bool try_confirm(stream_governor& gov, T& down,
                 const caf::strong_actor_ptr& rebind_from,
                 caf::strong_actor_ptr& hdl, long initial_demand) {
  if (down.confirm_path(rebind_from, hdl, initial_demand)) {
    CAF_LOG_DEBUG("Confirmed path to local worker" << hdl);
    auto path = down.find(hdl);
    if (path == nullptr) {
      CAF_LOG_ERROR("Cannot find worker after confirming it");
      return false;
    }
    gov.downstream_demand(path, initial_demand);
    return true;
  }
  return false;
}

} // namespace <anonymous>

caf::error
stream_governor::confirm_downstream(const caf::stream_id& sid,
                                    const caf::strong_actor_ptr& rebind_from,
                                    caf::strong_actor_ptr& hdl,
                                    long initial_demand, bool redeployable) {
  CAF_LOG_TRACE(CAF_ARG(rebind_from) << CAF_ARG(hdl)
                << CAF_ARG(initial_demand) << CAF_ARG(redeployable));
  CAF_IGNORE_UNUSED(redeployable);
  if (try_confirm(*this, workers_, rebind_from, hdl, initial_demand)
      || try_confirm(*this, stores_, rebind_from, hdl, initial_demand))
    return caf::none;
  auto i = input_to_peers_.find(sid);
  if (i == input_to_peers_.end()) {
    CAF_LOG_ERROR("Cannot confirm path to unknown downstream.");
    return caf::sec::invalid_downstream;
  }
  if (try_confirm(*this, i->second->out, rebind_from, hdl, initial_demand))
    return caf::none;
  return caf::sec::invalid_downstream;
}

caf::error stream_governor::downstream_ack(const caf::stream_id& sid,
                                           caf::strong_actor_ptr& hdl,
                                           int64_t batch_id, long demand) {
  CAF_LOG_TRACE(CAF_ARG(hdl) << CAF_ARG(demand));
  auto ack_path = [&](caf::downstream_policy& dp,
                      caf::downstream_path* path) -> caf::none_t {
    auto next_id = batch_id + 1;
    if (next_id > path->next_ack_id)
      path->next_ack_id = next_id;
    shutdown_if_at_end("Received last ACK");
    downstream_demand(path, demand);
    return caf::none;
  };
  auto wpath = workers_.find(hdl);
  if (wpath)
    return ack_path(workers_, wpath);
  auto spath = stores_.find(hdl);
  if (spath)
    return ack_path(stores_, spath);
  auto i = input_to_peers_.find(sid);
  if (i != input_to_peers_.end()) {
    auto ppath = i->second->out.find(hdl);
    if (!ppath)
      return caf::sec::invalid_stream_state;
    CAF_LOG_DEBUG("grant" << demand << "new credit to" << hdl);
    return ack_path(i->second->out, ppath);
  }
  return caf::sec::invalid_downstream;
}

caf::error stream_governor::push() {
  CAF_LOG_TRACE("");
  if (workers_.buf_size() > 0)
    workers_.emit_batches();
  if (stores_.buf_size() > 0)
    stores_.emit_batches();
  for (auto& kvp : peers_) {
    auto& out = kvp.second->out;
    if (out.buf_size() > 0)
      out.emit_batches();
  }
  return caf::none;
}

caf::expected<long> stream_governor::add_upstream(const caf::stream_id& relay_id,
                                                  caf::strong_actor_ptr& hdl,
                                                  const caf::stream_id& up_sid,
                                                  caf::stream_priority prio) {
  CAF_LOG_TRACE(CAF_ARG(hdl) << CAF_ARG(up_sid) << CAF_ARG(prio));
  if (!hdl)
    return caf::sec::invalid_argument;
  return in_.add_path(hdl, up_sid, prio, assignable_credit());
}

caf::error stream_governor::upstream_batch(const caf::stream_id& sid,
                                           caf::strong_actor_ptr& hdl,
                                           int64_t xs_id, long xs_size,
                                           caf::message& xs) {
  CAF_LOG_TRACE(CAF_ARG(sid) << CAF_ARG(hdl)
                << CAF_ARG(xs_size) << CAF_ARG(xs));
  // Sanity checking.
  auto path = in_.find(hdl);
  if (!path)
    return caf::sec::invalid_upstream;
  if (xs_size > path->assigned_credit)
    return caf::sec::invalid_stream_state;
  // Process messages from local workers.
  if (xs.match_elements<std::vector<worker_element>>()) {
    // Predicate for matching a single element against the filters of a peers.
    auto selected = [](const filter_type& f, const worker_element& x) -> bool {
      using std::get;
      for (auto& key : f)
        if (key == x.first)
          return true;
      return false;
    };
    auto& vec = xs.get_mutable_as<std::vector<worker_element>>(0);
    // Decrease credit assigned to `hdl` and get currently available downstream
    // credit on all paths.
    CAF_LOG_DEBUG(CAF_ARG(path->assigned_credit) << CAF_ARG(xs_size));
    path->last_batch_id = xs_id;
    path->assigned_credit -= xs_size;
    // Forward data to all other peers.
    for (auto& kvp : peers_)
      if (kvp.second->out.sid() != sid) {
        auto& out = kvp.second->out;
        for (const auto& x : vec)
          if (selected(kvp.second->filter, x))
            out.push(caf::make_message(x.first, x.second));
        if (out.buf_size() > 0) {
          out.emit_batches();
        }
      }
    /* Uncommenting this block forwards data from publishers to subscribers
     * inside an endpoint.
    // Move elements from `xs` to the buffer for local subscribers.
    CAF_LOG_DEBUG("local subs: " << workers_.num_paths());
    if (!workers_.lanes().empty())
      for (auto& x : vec)
        workers_.push(std::move(x));
    workers_.emit_batches();
    */
    // Grant new credit to upstream if possible.
    assign_credit();
    return caf::none;
  }
  // Process messages from peers.
  if (!xs.match_elements<std::vector<peer_element>>())
    return caf::sec::unexpected_message;
  // Predicate for matching a single element against the filters of a peers.
  auto selected = [](const filter_type& f, const peer_element& x) -> bool {
    CAF_ASSERT(x.size() == 2 && x.match_element<topic>(0));
    using std::get;
    for (auto& key : f)
      if (key == x.get_as<topic>(0))
        return true;
    return false;
  };
  // Unwrap `xs`.
  auto& vec = xs.get_mutable_as<std::vector<peer_element>>(0);
  // Decrease credit assigned to `hdl` and get currently available downstream
  // credit on all paths.
  CAF_LOG_DEBUG(CAF_ARG(path->assigned_credit) << CAF_ARG(xs_size));
  path->last_batch_id = xs_id;
  path->assigned_credit -= xs_size;
  // Forward data to all other peers.
  for (auto& kvp : peers_)
    if (kvp.second->out.sid() != sid) {
      auto& out = kvp.second->out;
      for (const auto& x : vec)
        if (selected(kvp.second->filter, x))
          out.push(x);
      if (out.buf_size() > 0) {
        out.emit_batches();
      }
    }
  // Move elements from `xs` to the buffer for local subscribers.
  if (!workers_.lanes().empty())
    for (auto& x : vec)
      if (x.match_element<data>(1)) {
        x.force_unshare();
        workers_.push(std::make_pair(x.get_as<topic>(0),
                                     std::move(x.get_mutable_as<data>(1))));
      }
  workers_.emit_batches();
  if (!stores_.lanes().empty())
    for (auto& x : vec)
      if (x.match_element<internal_command>(1)) {
        x.force_unshare();
        stores_.push(
          std::make_pair(x.get_as<topic>(0),
                         std::move(x.get_mutable_as<internal_command>(1))));
      }
  stores_.emit_batches();
  // Grant new credit to upstream if possible.
  CAF_LOG_DEBUG("pushed data to" << peers_.size() << "peers,"
                << workers_.paths().size() << "workers, and"
                << workers_.paths().size() << "stores");
  auto available = downstream_credit();
  if (available > 0)
    in_.assign_credit(available);
  return caf::none;
}

caf::error stream_governor::close_upstream(const caf::stream_id& sid,
                                           caf::strong_actor_ptr& hdl) {
  CAF_LOG_TRACE(CAF_ARG(hdl));
  if (in_.remove_path(hdl)) {
    auto& li = state_->local_sources;
    auto i = li.find(sid);
    if (i != li.end()) {
      CAF_LOG_DEBUG("local upstream closed:" << CAF_ARG(sid));
      li.erase(i);
      shutdown_if_at_end("Closed last local input");
    }
    return caf::none;
  }
  return caf::sec::invalid_upstream;
}

void stream_governor::abort(const caf::stream_id& sid,
                            caf::strong_actor_ptr& hdl,
                            const caf::error& reason) {
  CAF_LOG_TRACE(CAF_ARG(hdl) << CAF_ARG(reason));
  if (hdl == nullptr) {
    // actor shutdown
    if (!workers_.lanes().empty())
      workers_.abort(hdl, reason);
    if (!stores_.lanes().empty())
      stores_.abort(hdl, reason);
    if (!peers_.empty()) {
      for (auto& kvp : peers_)
        kvp.second->out.abort(hdl, reason);
      input_to_peers_.clear();
      peers_.clear();
    }
    in_.abort(hdl, reason);
    return;
  }
  if (workers_.remove_path(hdl) || stores_.remove_path(hdl)) {
    push();
    assign_credit();
    shutdown_if_at_end("Aborted last local sink");
    return;
  }
  // Check if hdl is a local source.
  { // Lifetime scope of state_->local_sources iterator i
    auto i = state_->local_sources.find(sid);
    if (i != state_->local_sources.end()) {
      CAF_LOG_DEBUG("Abort from local source.");
      // Do not propagate errors of local actors.
      in_.remove_path(hdl);
      state_->local_sources.erase(i);
      push();
      assign_credit();
      shutdown_if_at_end("Aborted last local source");
      return;
    }
  }
  // Check if this stream ID (sid) is used by a peer for sending data to us.
  { // Lifetime scope of input_to_peers_ iterator i
    auto i = input_to_peers_.find(sid);
    if (i != input_to_peers_.end()) {
      CAF_LOG_DEBUG("Abort from remote source (peer).");
      in_.remove_path(hdl);
      auto pptr = std::move(i->second);
      input_to_peers_.erase(i);
      // Remove this peer entirely if no downstream to it exists.
      if (pptr->out.num_paths() == 0) {
        CAF_LOG_DEBUG("Remove peer: up- and downstream severed.");
        state_->emit_status<sc::peer_lost>(pptr->remote_core,
                                           "lost remote peer");
        peers_.erase(pptr->remote_core);
      }
      push();
      assign_credit();
      return;
    }
  }
  // Check if this stream ID (sid) is used by a peer for receiving data from us.
  { // Lifetime scope of  peers_ iterators
    auto predicate = [&](const peer_map::value_type& kvp) {
      return kvp.second->out.sid() == sid;
    };
    auto e = peers_.end();
    auto i = std::find_if(peers_.begin(), e, predicate);
    if (i != e) {
      auto& pd = *i->second;
      pd.out.remove_path(hdl);
      // Remove this peer entirely if no upstream from it exists.
      if (input_to_peers_.count(pd.incoming_sid) == 0) {
        state_->emit_status<sc::peer_lost>(pd.remote_core,
                                           "lost remote peer");
        CAF_LOG_DEBUG("Remove peer: down- and upstream severed.");
        peers_.erase(i);
        // Shutdown when the last peer stops listening.
        if (state_->shutting_down && peers_.empty())
          state_->self->quit(caf::exit_reason::user_shutdown);
      }
      assign_credit();
      push();
      return;
    }
  }
  CAF_LOG_DEBUG("Abort from unknown stream ID.");
}

long stream_governor::downstream_credit() const {
  auto min_peer_credit = [&] {
    return std::accumulate(peers_.begin(), peers_.end(),
                           std::numeric_limits<long>::max(),
                           [](long x, const peer_map::value_type& y) {
                             return std::min(x, y.second->out.min_credit());
                           });
  };
  // TODO: make configurable and/or adaptive
  constexpr long min_buffer_size = 5l;
  auto result = min_peer_credit(); // max long value if no peer exists
  if (workers_.num_paths() > 0)
    result = std::min(result, workers_.min_credit());
  if (stores_.num_paths() > 0)
    result = std::min(result, stores_.min_credit());
  return (result == std::numeric_limits<long>::max() ? 0l : result) 
         + min_buffer_size;
}

void stream_governor::close_remote_input() {
  CAF_LOG_TRACE("");
  auto sid = workers_.sid();
  std::vector<caf::strong_actor_ptr> remotes;
  for (auto& path : in_.paths())
    if (state_->local_sources.count(path->sid) == 0)
      remotes.emplace_back(path->hdl);
  CAF_LOG_DEBUG(CAF_ARG(remotes) << CAF_ARG(state_->local_sources));
  caf::strong_actor_ptr dummy;
  for (auto& sap : remotes)
    in_.remove_path(sap, caf::exit_reason::user_shutdown);
  input_to_peers_.clear();
}

bool stream_governor::at_end() const {
  return state_->shutting_down
         && state_->local_sources.empty()
         && workers_.closed()
         && stores_.closed()
         && no_data_pending();
}

bool stream_governor::no_data_pending() const {
  auto path_clean = [](const caf::downstream_policy::path_uptr& ptr) {
    return ptr->next_batch_id == ptr->next_ack_id;
  };
  auto all_acked = [&](const caf::downstream_policy& dp) {
    return std::all_of(dp.paths().begin(), dp.paths().end(), path_clean);
  };
  auto peer_all_acked = [&](const peer_map::value_type& kvp) {
    return all_acked(kvp.second->out);
  };
  return all_acked(workers_)
         && all_acked(stores_)
         && std::all_of(peers_.begin(), peers_.end(), peer_all_acked);
}

void stream_governor::shutdown_if_at_end(const char* log_message) {
  CAF_IGNORE_UNUSED(log_message);
  if (at_end()) {
    CAF_LOG_DEBUG(log_message);
    state_->self->quit(caf::exit_reason::user_shutdown);
  }
}

long stream_governor::downstream_buffer_size() const {
  auto result = std::max(workers_.buf_size(), stores_.buf_size());
  for (auto& kvp : peers_)
    result += std::max(result, kvp.second->out.buf_size());
  return result;
}

void stream_governor::assign_credit() {
  CAF_LOG_TRACE("");
  auto x = assignable_credit();
  if (x > 0)
    in_.assign_credit(x);
}

long stream_governor::assignable_credit() {
  auto current_size = downstream_buffer_size();
  auto desired_size = downstream_credit();
  CAF_LOG_DEBUG(CAF_ARG(current_size) << CAF_ARG(desired_size));
  return current_size < desired_size ? desired_size - current_size : 0l;
}

void intrusive_ptr_add_ref(stream_governor* x) {
  x->ref();
}

void intrusive_ptr_release(stream_governor* x) {
  x->deref();
}

} // namespace detail
} // namespace broker
