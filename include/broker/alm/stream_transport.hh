#pragma once

#include <unordered_map>
#include <vector>

#include <caf/async/publisher.hpp>
#include <caf/cow_tuple.hpp>
#include <caf/detail/scope_guard.hpp>
#include <caf/detail/unordered_flat_map.hpp>
#include <caf/event_based_actor.hpp>
#include <caf/flow/merge.hpp>

#include "broker/alm/peer.hh"
#include "broker/alm/routing_table.hh"
#include "broker/detail/connector.hh"
#include "broker/detail/connector_adapter.hh"
#include "broker/detail/flow_controller.hh"
#include "broker/detail/hash.hh"
#include "broker/detail/lift.hh"
#include "broker/detail/peer_status_map.hh"
#include "broker/detail/prefix_matcher.hh"
#include "broker/error.hh"
#include "broker/filter_type.hh"
#include "broker/fwd.hh"
#include "broker/logger.hh"
#include "broker/message.hh"
#include "broker/shutdown_options.hh"

namespace broker::alm {

class dispatch_step;

/// The transport registers these message handlers:
///
/// ~~~
/// (atom::peer, endpoint_id, actor) -> void
/// => start_peering(id, hdl)
///
/// (atom::peer, atom::init, endpoint_id, actor) -> slot
/// => handle_peering_request(...)
///
/// (stream<node_message>, actor, endpoint_id, filter_type, lamport_timestamp) -> slot
/// => handle_peering_handshake_1(...)
///
/// (stream<node_message>, actor, endpoint_id) -> void
/// => handle_peering_handshake_2(...)
///
/// (atom::unpeer, actor hdl) -> void
/// => disconnect(hdl)
/// ~~~
class stream_transport : public peer, public detail::flow_controller {
public:
  // -- friends ----------------------------------------------------------------

  friend class dispatch_step;

  // -- member types -----------------------------------------------------------

  using super = peer;

  using node_message_publisher = caf::async::publisher<node_message>;

  using connect_flows_fun
    = std::function<node_message_publisher(node_message_publisher)>;

  // -- constructors, destructors, and assignment operators --------------------

  explicit stream_transport(caf::event_based_actor* self);

  stream_transport(caf::event_based_actor* self, detail::connector_ptr conn);

  // -- properties -------------------------------------------------------------

  /// Returns the @ref network_info associated to given `id` if available.
  const network_info* addr_of(endpoint_id id) const noexcept;

  // -- overrides for peer::publish --------------------------------------------

  void publish(endpoint_id dst, atom::subscribe, const endpoint_id_list& path,
               const vector_timestamp& ts,
               const filter_type& new_filter) override;

  void publish(endpoint_id dst, atom::revoke, const endpoint_id_list& path,
               const vector_timestamp& ts, const endpoint_id& lost_peer,
               const filter_type& new_filter) override;

  using super::publish_locally;

  void publish_locally(const data_message& msg) override;

  void publish_locally(const command_message& msg) override;

  void dispatch(const data_message& msg) override;

  void dispatch(const command_message& msg) override;

  void dispatch(alm::multipath path, const data_message& msg) override;

  void dispatch(alm::multipath path, const command_message& msg) override;

  // -- overrides for alm::peer ------------------------------------------------

  void shutdown(shutdown_options options) override;

  // -- overrides for flow_controller ------------------------------------------

  caf::scheduled_actor* ctx() override;

  void add_source(caf::flow::observable<data_message> source) override;

  void add_source(caf::flow::observable<command_message> source) override;

  void add_sink(caf::flow::observer<data_message> sink) override;

  void add_sink(caf::flow::observer<command_message> sink) override;

  caf::async::publisher<data_message>
  select_local_data(const filter_type& filter) override;

  caf::async::publisher<command_message>
  select_local_commands(const filter_type& filter) override;

  void add_filter(const filter_type& filter) override;

  // -- initialization ---------------------------------------------------------

  caf::behavior make_behavior() override;

  caf::error init_new_peer(endpoint_id peer, const network_info& addr,
                           alm::lamport_timestamp ts, const filter_type& filter,
                           connect_flows_fun connect_flows);

  caf::error init_new_peer(endpoint_id peer, const network_info& addr,
                           alm::lamport_timestamp ts, const filter_type& filter,
                           caf::net::stream_socket sock);

  /// @private
  template <class T>
  packed_message pack(const T& msg);

protected:
  // -- utility ----------------------------------------------------------------

  struct peer_state {
    caf::disposable in;
    caf::disposable out;
    network_info addr;
    bool invalidated = false;

    peer_state() = delete;

    peer_state(caf::disposable in, caf::disposable out, network_info addr)
      : in(std::move(in)), out(std::move(out)), addr(std::move(addr)) {
      // nop
    }
    peer_state(peer_state&&) = default;
    peer_state& operator=(peer_state&&) = default;
  };

  using peer_state_map = std::map<endpoint_id, peer_state>;

  peer_state_map::iterator find_peer(const network_info& addr) noexcept;

  /// Disconnects a peer by demand of the user.
  void unpeer(const endpoint_id& peer_id);

  /// Disconnects a peer by demand of the user.
  void unpeer(const network_info& peer_addr);

  /// Disconnects a peer by demand of the user.
  void unpeer(peer_state_map::iterator i);

  /// Initialized the `data_outputs_` member lazily.
  void init_data_outputs();

  /// Initialized the `command_outputs_` member lazily.
  void init_command_outputs();

  /// Collects inputs from @ref broker::publisher objects.
  caf::flow::merger_impl_ptr<data_message> data_inputs_;

  /// Collects inputs from data store objects.
  caf::flow::merger_impl_ptr<command_message> command_inputs_;

  /// Provides central access to packed messages with routing information.
  caf::flow::merger_impl_ptr<node_message> central_merge_;

  /// Provides access to local @ref broker::subscriber objects.
  caf::flow::observable<data_message> data_outputs_;

  /// Provides access to local data store objects.
  caf::flow::observable<command_message> command_outputs_;

  /// Handle to the background worker for establishing peering relations.
  std::unique_ptr<detail::connector_adapter> connector_adapter_;

  /// Handles for aborting flows on unpeering.
  peer_state_map peers_;

  /// Synchronizes information about the current status of a peering with the
  /// connector.
  detail::shared_peer_status_map_ptr peers_statuses_
    = std::make_shared<detail::peer_status_map>();

  /// Buffer for serializing messages.
  caf::byte_buffer buf_;

  /// Caches the reserved topic for peer-to-peer control messages.
  topic reserved_;
};

} // namespace broker::alm
