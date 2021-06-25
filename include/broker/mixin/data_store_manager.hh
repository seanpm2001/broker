#pragma once

#include <string>
#include <unordered_map>

#include <caf/actor.hpp>
#include <caf/behavior.hpp>

#include "broker/backend.hh"
#include "broker/backend_options.hh"
#include "broker/detail/clone_actor.hh"
#include "broker/detail/lift.hh"
#include "broker/detail/make_backend.hh"
#include "broker/detail/master_actor.hh"
#include "broker/detail/master_resolver.hh"
#include "broker/endpoint.hh"
#include "broker/filter_type.hh"
#include "broker/logger.hh"
#include "broker/shutdown_options.hh"
#include "broker/topic.hh"

namespace broker::mixin {

template <class Base>
class data_store_manager : public Base {
public:
  // --- member types ----------------------------------------------------------

  using super = Base;

  using extended_base = data_store_manager;

  // --- constants -------------------------------------------------------------

  static constexpr auto spawn_flags = caf::linked + caf::lazy_init;

  // --- construction and destruction ------------------------------------------

  template <class... Ts>
  data_store_manager(caf::event_based_actor* self, endpoint::clock* clock,
                     Ts&&... xs)
    : super(self, std::forward<Ts>(xs)...), clock_(clock) {
    // nop
  }

  data_store_manager() = delete;

  data_store_manager(const data_store_manager&) = delete;

  data_store_manager& operator=(const data_store_manager&) = delete;

  // -- properties -------------------------------------------------------------

  /// Returns whether a master for `name` probably exists already on one of our
  /// peers.
  bool has_remote_master(const std::string& name) {
    // If we don't have a master recorded locally, we could still have a
    // propagated filter to a remote core hosting a master.
    return this->has_remote_subscriber(name / topics::master_suffix);
  }

  const auto& masters() const noexcept {
    return masters_;
  }

  const auto& clones() const noexcept {
    return clones_;
  }

  // -- data store management --------------------------------------------------

  /// Attaches a master for given store to this peer.
  caf::result<caf::actor> attach_master(const std::string& name,
                                        backend backend_type,
                                        backend_options opts) {
    // TODO: implement me
    return ec::unspecified;
    // BROKER_TRACE(BROKER_ARG(name)
    //              << BROKER_ARG(backend_type) << BROKER_ARG(opts));
    // if (auto i = masters_.find(name); i != masters_.end())
    //   return i->second;
    // if (has_remote_master(name)) {
    //   BROKER_WARNING("remote master with same name exists already");
    //   return ec::master_exists;
    // }
    // auto ptr = detail::make_backend(backend_type, std::move(opts));
    // BROKER_ASSERT(ptr != nullptr);
    // BROKER_INFO("spawning new master:" << name);
    // auto self = super::self();
    // auto ms = self->template spawn<spawn_flags>(
    //   detail::master_actor, this->id(), self, name, std::move(ptr), clock_);
    // filter_type filter{name / topics::master_suffix};
    // if (auto err = this->add_store(ms, filter))
    //   return err;
    // masters_.emplace(name, ms);
    // return ms;
  }

  /// Attaches a clone for given store to this peer.
  caf::result<caf::actor>
  attach_clone(const std::string& name, double resync_interval,
               double stale_interval, double mutation_buffer_interval) {
    // TODO: implement me
    return ec::unspecified;
    // BROKER_TRACE(BROKER_ARG(name)
    //              << BROKER_ARG(resync_interval) << BROKER_ARG(stale_interval)
    //              << BROKER_ARG(mutation_buffer_interval));
    // if (auto i = masters_.find(name); i != masters_.end()) {
    //   BROKER_WARNING("attempted to run clone & master on the same endpoint");
    //   return ec::no_such_master;
    // }
    // if (auto i = clones_.find(name); i != clones_.end())
    //   return i->second;
    // BROKER_INFO("spawning new clone:" << name);
    // auto self = super::self();
    // auto cl = self->template spawn<spawn_flags>(
    //   detail::clone_actor, this->id(), self, name, resync_interval,
    //   stale_interval, mutation_buffer_interval, clock_);
    // filter_type filter{name / topics::clone_suffix};
    // if (auto err = this->add_store(cl, filter))
    //   return err;
    // clones_.emplace(name, cl);
    // return cl;
  }

  /// Returns whether the master for the given store runs at this peer.
  caf::result<caf::actor> get_master(const std::string& name) {
    auto i = masters_.find(name);
    if (i != masters_.end())
      return i->second;
    return ec::no_such_master;
  }

  /// Detaches all masters and clones by sending exit messages to the
  /// corresponding actors.
  void detach_stores() {
    BROKER_TRACE(BROKER_ARG2("masters_.size()", masters_.size())
                 << BROKER_ARG2("clones_.size()", clones_.size()));
    auto self = super::self();
    auto f = [&](auto& container) {
      for (auto& kvp : container) {
        self->send_exit(kvp.second, caf::exit_reason::kill);
        // TODO: re-implement graceful shutdown
        // self->send_exit(kvp.second, caf::exit_reason::user_shutdown);
      }
      container.clear();
    };
    f(masters_);
    f(clones_);
  }

  // -- overrides --------------------------------------------------------------

  void shutdown(shutdown_options options) override {
    detach_stores();
    super::shutdown(options);
  }

  // -- factories --------------------------------------------------------------

  caf::behavior make_behavior() override {
    using detail::lift;
    return caf::message_handler{
      lift<atom::store, atom::clone, atom::attach>(
        *this, &data_store_manager::attach_clone),
      lift<atom::store, atom::master, atom::attach>(
        *this, &data_store_manager::attach_master),
      lift<atom::store, atom::master, atom::get>(
        *this, &data_store_manager::get_master),
      lift<atom::shutdown, atom::store>(*this,
                                        &data_store_manager::detach_stores),
      [this](atom::store, atom::master, atom::resolve, std::string& name,
             caf::actor& who_asked) {
        // TODO: get rid of the who_asked parameter and use proper
        // request/response semantics with forwarding/dispatching
        auto self = super::self();
        auto i = masters_.find(name);
        if (i != masters_.end()) {
          self->send(who_asked, atom::master_v, i->second);
          return;
        }
        auto peers = this->peer_handles();
        if (peers.empty()) {
          BROKER_INFO("no peers to ask for the master");
          self->send(who_asked, atom::master_v,
                     make_error(ec::no_such_master, "no peers"));
          return;
        }
        auto resolver
          = self->template spawn<spawn_flags>(detail::master_resolver);
        self->send(resolver, std::move(peers), std::move(name),
                   std::move(who_asked));
      },
    }
      .or_else(super::make_behavior());
  }

private:
  // -- member variables -------------------------------------------------------

  /// Enables manual time management by the user.
  endpoint::clock* clock_;

  /// Stores all master actors created by this core.
  std::unordered_map<std::string, caf::actor> masters_;

  /// Stores all clone actors created by this core.
  std::unordered_map<std::string, caf::actor> clones_;
};

} // namespace broker::mixin
