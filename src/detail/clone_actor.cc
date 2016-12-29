#include "broker/logger.hh" // Needs to come before CAF includes.

#include <caf/all.hpp>

#include "broker/atoms.hh"
#include "broker/convert.hh"
#include "broker/data.hh"
#include "broker/expected.hh"
#include "broker/snapshot.hh"
#include "broker/status.hh"
#include "broker/topic.hh"

#include "broker/detail/clone_actor.hh"
#include "broker/detail/appliers.hh"

namespace broker {
namespace detail {

caf::behavior clone_actor(caf::stateful_actor<clone_state>* self,
                          caf::actor core, caf::actor master,
                          std::string name) {
  auto forward = [=](const caf::message& msg) {
    auto t = name / topics::reserved / topics::master;
    self->send(master, std::move(t), msg, core);
  };
  auto relay = caf::message_handler{
    [=](atom::put, data& key, data& value, optional<timestamp> expiry) {
      forward(caf::make_message(atom::put::value, std::move(key),
                                std::move(value), expiry));
    },
    [=](atom::add, data& key, data& value, optional<timestamp> expiry) {
      forward(caf::make_message(atom::add::value, std::move(key),
                                std::move(value), expiry));
    },
    [=](atom::remove, data& key, data& value, optional<timestamp> expiry) {
      forward(caf::make_message(atom::remove::value, std::move(key),
                                std::move(value), expiry));
    },
    [=](atom::erase, data& key) {
      forward(caf::make_message(atom::erase::value, std::move(key)));
    },
  };
  auto update = caf::message_handler{
    [=](atom::put, data& key, data& value) {
      BROKER_DEBUG("put" << key << "->" << value);
      auto i = self->state.store.find(key);
      if (i != self->state.store.end())
        i->second = std::move(value);
      else
        self->state.store.emplace(std::move(key), std::move(value));
    },
    [=](atom::add, data& key, data& value) {
      BROKER_DEBUG("add" << key << "->" << value);
      auto i = self->state.store.find(key);
      if (i == self->state.store.end()) {
        self->state.store.emplace(std::move(key), std::move(value));
      } else {
        auto result = visit(adder{value}, i->second);
        BROKER_ASSERT(result); // We don't propagate errors.
      }
    },
    [=](atom::remove, data& key, data& value) {
      BROKER_DEBUG("remove" << key << "->" << value);
      auto i = self->state.store.find(key);
      BROKER_ASSERT(i != self->state.store.end());
      auto result = visit(remover{value}, i->second);
      BROKER_ASSERT(result); // We don't propagate errors.
    },
    [=](atom::erase, data& key) {
      BROKER_DEBUG("erase" << key);
      self->state.store.erase(key);
    },
  };
  auto dispatch = caf::message_handler{
    [=](topic& t, caf::message& msg, const caf::actor& source) mutable {
      BROKER_DEBUG("dispatching message with topic" << t << "from core"
                   << to_string(source));
      update(msg);
    }
  };
  auto query = caf::message_handler{
    [=](atom::get, const data& key) -> expected<data> {
      BROKER_DEBUG("got GET" << key);
      auto i = self->state.store.find(key);
      if (i == self->state.store.end())
        return sc::no_such_key;
      return i->second;
    },
    [=](atom::get, const data& key, const data& value) -> expected<data> {
      BROKER_DEBUG("GET" << key << "->" << value);
      auto i = self->state.store.find(key);
      if (i == self->state.store.end())
        return sc::no_such_key;
      return visit(retriever{value}, i->second);
    },
    [=](atom::get, atom::name) {
      return name;
    },
  };
  auto direct = caf::message_handler{
    [=](snapshot& ss) {
      BROKER_DEBUG("got snapshot with" << ss.entries.size() << "entries");
      self->state.store = ss.entries;
    },
  };
  return dispatch.or_else(relay).or_else(update).or_else(query).or_else(direct);
}

} // namespace detail
} // namespace broker

