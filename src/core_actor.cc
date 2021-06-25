#include "broker/core_actor.hh"

#include <caf/actor.hpp>
#include <caf/actor_cast.hpp>
#include <caf/allowed_unsafe_message_type.hpp>
#include <caf/behavior.hpp>
#include <caf/error.hpp>
#include <caf/event_based_actor.hpp>
#include <caf/exit_reason.hpp>
#include <caf/group.hpp>
#include <caf/make_counted.hpp>
#include <caf/none.hpp>
#include <caf/response_promise.hpp>
#include <caf/result.hpp>
#include <caf/sec.hpp>
#include <caf/spawn_options.hpp>
#include <caf/stateful_actor.hpp>
#include <caf/stream.hpp>
#include <caf/stream_slot.hpp>
#include <caf/system_messages.hpp>
#include <caf/unit.hpp>

#include "broker/domain_options.hh"

namespace broker {

core_state::core_state(caf::event_based_actor* self, endpoint_id this_peer,
                       filter_type initial_filter, endpoint::clock* clock,
                       const domain_options* adaptation,
                       detail::connector_ptr conn)
  : super(self, clock, std::move(conn)) {
  id(this_peer);
  if (adaptation && adaptation->disable_forwarding)
    disable_forwarding(true);
  if (!initial_filter.empty())
    subscribe(initial_filter);
}

core_state::~core_state() {
  BROKER_DEBUG("core_state destroyed");
}

caf::behavior core_state::make_behavior() {
  self_->set_exit_handler([self{self_}](caf::exit_msg& msg) {
    if (msg.reason) {
      BROKER_DEBUG("shutting down after receiving an exit message with reason:"
                   << msg.reason);
      self->quit(std::move(msg.reason));
    }
  });
  auto& cfg = self_->system().config();
  cache().set_use_ssl(!caf::get_or(cfg, "broker.disable-ssl", false));
  return caf::message_handler{
    [=](atom::get, atom::peer) {
      std::vector<peer_info> result;
      // Add all direct connections from the routing table.
      alm::for_each_direct(tbl(), [&, this](const auto& id, const auto& hdl) {
        endpoint_info ep{id, cache().find(hdl)};
        result.push_back(
          {std::move(ep), peer_flags::remote, peer_status::peered});
      });
      // TODO: implement me
      // // Add all pending peerings from the stream transport.
      // for (const auto& [peer_id, pending_conn] : pending_connections()) {
      //   endpoint_info ep{peer_id, cache().find(pending_conn->remote_hdl)};
      //   result.push_back(
      //     {std::move(ep), peer_flags::remote, peer_status::connected});
      // }
      return result;
    },
  }
    .or_else(super::make_behavior());
}

} // namespace broker
