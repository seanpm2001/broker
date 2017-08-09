
#include <pybind11/functional.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl_bind.h>

#include "broker/broker.hh"

namespace py = pybind11;

extern void init_bro(py::module& m);
extern void init_data(py::module& m);
extern void init_enums(py::module& m);
extern void init_store(py::module& m);

PYBIND11_MAKE_OPAQUE(broker::set);
PYBIND11_MAKE_OPAQUE(broker::table);
PYBIND11_MAKE_OPAQUE(broker::vector);

PYBIND11_PLUGIN(_broker) {
  py::module m{"_broker", "Broker python bindings"};
  py::module mb = m.def_submodule("bro", "Bro-specific bindings");

  init_bro(mb);
  init_enums(m);
  init_data(m);
  init_store(m);

  auto version = m.def_submodule("Version", "Version constants");
  version.attr("MAJOR")
    = py::cast(new broker::version::type{broker::version::major});
  version.attr("MINOR")
    = py::cast(new broker::version::type{broker::version::minor});
  version.attr("PATCH")
    = py::cast(new broker::version::type{broker::version::patch});
  version.attr("PROTOCOL")
    = py::cast(new broker::version::type{broker::version::protocol});
  version.def("compatible", &broker::version::compatible,
              "Checks whether two Broker protocol versions are compatible");

  m.def("now", &broker::now, "Get the current wallclock time");

  py::class_<broker::endpoint_info>(m, "EndpointInfo")
    .def_readwrite("node", &broker::endpoint_info::node)
    // TODO: Can we convert this optional<network_info> directly into network_info or None?
    .def_readwrite("network", &broker::endpoint_info::network);

  py::class_<broker::network_info>(m, "NetworkInfo")
    .def_readwrite("address", &broker::network_info::address)
    .def_readwrite("port", &broker::network_info::port)
    .def("__repr__", [](const broker::network_info& n) { return to_string(n); });

  py::class_<broker::optional<broker::network_info>>(m, "OptionalNetworkInfo")
    .def("is_set",
         [](broker::optional<broker::network_info>& i) { return static_cast<bool>(i);})
    .def("get",
         [](broker::optional<broker::network_info>& i) { return *i; })
    .def("__repr__", [](const broker::optional<broker::network_info>& i) { return to_string(i); });

  py::class_<broker::peer_info>(m, "PeerInfo")
    .def_readwrite("peer", &broker::peer_info::peer)
    .def_readwrite("flags", &broker::peer_info::flags)
    .def_readwrite("status", &broker::peer_info::status);

  py::class_<broker::topic>(m, "Topic")
    .def(py::init<std::string>())
    .def(py::self /= py::self,
         "Appends a topic component with a separator")
    .def(py::self / py::self,
         "Appends topic components with a separator")
    .def("string", &broker::topic::string,
         "Get the underlying string representation of the topic",
         py::return_value_policy::reference_internal)
    .def("__repr__", [](const broker::topic& t) { return t.string(); });

  py::bind_vector<std::vector<broker::topic>>(m, "VectorTopic");

  py::class_<broker::infinite_t>(m, "Infinite")
    .def(py::init<>());

  py::class_<broker::publisher>(m, "Publisher")
    .def("demand", &broker::publisher::demand)
    .def("buffered", &broker::publisher::buffered)
    .def("capacity", &broker::publisher::capacity)
    .def("free_capacity", &broker::publisher::free_capacity)
    .def("send_rate", &broker::publisher::send_rate)
    .def("fd", &broker::publisher::fd)
    .def("drop_all_on_destruction", &broker::publisher::drop_all_on_destruction)
    .def("publish", (void (broker::publisher::*)(broker::data d)) &broker::publisher::publish)
    .def("publish_batch",
       [](broker::publisher& p, std::vector<broker::data> xs) { p.publish(xs); });

  using subscriber_base = broker::subscriber_base<broker::subscriber::value_type>;

  py::bind_vector<std::vector<subscriber_base::value_type>>(m, "VectorPairTopicData");

  py::class_<subscriber_base>(m, "SubscriberBase")
    .def("get", (subscriber_base::value_type (subscriber_base::*)()) &subscriber_base::get)
    .def("get",
         [](subscriber_base& ep, double secs) -> broker::optional<subscriber_base::value_type> {
	   return ep.get(broker::to_duration(secs)); })
    .def("get",
         [](subscriber_base& ep, size_t num) -> std::vector<subscriber_base::value_type> {
	   return ep.get(num); })
    .def("get",
         [](subscriber_base& ep, size_t num, double secs) -> std::vector<subscriber_base::value_type> {
	   return ep.get(num, broker::to_duration(secs)); })
    .def("poll", &subscriber_base::poll)
    .def("available", &subscriber_base::available)
    .def("fd", &subscriber_base::fd);

  py::class_<broker::subscriber, subscriber_base>(m, "Subscriber")
    .def("add_topic", &broker::subscriber::add_topic)
    .def("remove_topic", &broker::subscriber::remove_topic);

  using event_subscriber_base = broker::subscriber_base<broker::event_subscriber::value_type>;

  py::bind_vector<std::vector<event_subscriber_base::value_type>>(m, "VectorEventSubscriberValueType");

  py::class_<event_subscriber_base>(m, "EventSubscriberBase")
    .def("get", (event_subscriber_base::value_type (event_subscriber_base::*)()) &event_subscriber_base::get)
    .def("get",
         [](event_subscriber_base& ep, double secs) -> broker::optional<event_subscriber_base::value_type> {
	   return ep.get(broker::to_duration(secs)); })
    .def("get",
         [](event_subscriber_base& ep, size_t num) -> std::vector<event_subscriber_base::value_type> {
	   return ep.get(num); })
    .def("get",
         [](event_subscriber_base& ep, size_t num, double secs) -> std::vector<event_subscriber_base::value_type> {
	   return ep.get(num, broker::to_duration(secs)); })
    .def("poll",
         [](event_subscriber_base& ep) -> std::vector<event_subscriber_base::value_type> {
	   return ep.poll(); })
    .def("available", &event_subscriber_base::available)
    .def("fd", &event_subscriber_base::fd);

  py::class_<broker::status>(m, "Status")
    .def(py::init<>())
    .def("code", &broker::status::code)
    .def("context", &broker::status::context<broker::endpoint_info>,
	 py::return_value_policy::reference_internal)
    .def("__repr__", [](const broker::status& s) { return to_string(s); });

  py::class_<broker::error>(m, "Error")
    .def(py::init<>())
    .def("code", &broker::error::code)
    .def("__repr__", [](const broker::error& e) { return to_string(e); });

  py::class_<broker::event_subscriber, event_subscriber_base> event_subscriber(m, "EventSubscriber");

  py::class_<broker::event_subscriber::value_type>(event_subscriber, "ValueType")
    .def("is_error",
         [](broker::event_subscriber::value_type& x) -> bool { return broker::is<broker::error>(x);})
    .def("is_status",
         [](broker::event_subscriber::value_type& x) -> bool { return broker::is<broker::status>(x);})
    .def("get_error",
         [](broker::event_subscriber::value_type& x) -> broker::error { return broker::get<broker::error>(x);})
    .def("get_status",
         [](broker::event_subscriber::value_type& x) -> broker::status { return broker::get<broker::status>(x);});

  py::bind_map<broker::backend_options>(m, "MapBackendOptions");

  // We need a configuration class here that's separate from
  // broker::configuration. When creating an endpoint one has to instantiate
  // the standard class right at that point, one cannot pass an already
  // created one in, which is unfortunate.
  struct Configuration {
    Configuration(bool disable_ssl=false) : disable_ssl(disable_ssl) {}
    bool disable_ssl;
    std::string openssl_cafile;
    std::string openssl_capath;
    std::string openssl_certificate;
    std::string openssl_key;
    std::string openssl_passphrase;
  };

  py::class_<Configuration>(m, "Configuration")
    .def(py::init<>())
    .def(py::init<bool>())
    .def_readwrite("openssl_cafile", &Configuration::openssl_cafile)
    .def_readwrite("openssl_capath", &Configuration::openssl_capath)
    .def_readwrite("openssl_certificate", &Configuration::openssl_certificate)
    .def_readwrite("openssl_key", &Configuration::openssl_key)
    .def_readwrite("openssl_passphrase", &Configuration::openssl_passphrase);

  py::class_<broker::endpoint>(m, "Endpoint")
    .def(py::init<>())
    .def("__init__",
       [](broker::endpoint& ep, Configuration cfg) {
       auto make_config = [&]() {
           broker::configuration bcfg;
	   bcfg.openssl_capath = cfg.openssl_capath;
	   bcfg.openssl_passphrase = cfg.openssl_passphrase;
           bcfg.openssl_cafile = cfg.openssl_cafile;
           bcfg.openssl_certificate = cfg.openssl_certificate;
           bcfg.openssl_key = cfg.openssl_key;
           return bcfg;
	   };
       new (&ep) broker::endpoint(make_config());
       })
    .def("listen", &broker::endpoint::listen, py::arg("address"), py::arg("port") = 0)
    .def("peer",
         [](broker::endpoint& ep, std::string& addr, uint16_t port, double retry) -> bool {
	 return ep.peer(addr, port, std::chrono::seconds((int)retry));},
         py::arg("addr"), py::arg("port"), py::arg("retry") = 10.0
         )
    .def("peer_nosync",
         [](broker::endpoint& ep, std::string& addr, uint16_t port, double retry) {
	 ep.peer_nosync(addr, port, std::chrono::seconds((int)retry));},
         py::arg("addr"), py::arg("port"), py::arg("retry") = 10.0
         )
    .def("unpeer", &broker::endpoint::peer)
    .def("peers", &broker::endpoint::peers)
    .def("peer_subscriptions", &broker::endpoint::peer_subscriptions)
    .def("publish", (void (broker::endpoint::*)(broker::topic t, broker::data d)) &broker::endpoint::publish)
    .def("publish", (void (broker::endpoint::*)(const broker::endpoint_info& dst, broker::topic t, broker::data d)) &broker::endpoint::publish)
    .def("publish_batch",
       [](broker::endpoint& ep, std::vector<broker::endpoint::value_type> xs) { ep.publish(xs); })
    .def("make_publisher", &broker::endpoint::make_publisher)
    .def("make_subscriber", &broker::endpoint::make_subscriber, py::arg("topics"), py::arg("max_qsize") = 20)
    .def("make_event_subscriber", &broker::endpoint::make_event_subscriber, py::arg("receive_statuses") = false)
    .def("shutdown", &broker::endpoint::shutdown)
    .def("attach",
         [](broker::endpoint& ep, const std::string& name, broker::backend type,
            const broker::backend_options& opts) -> broker::expected<broker::store> {
	        return ep.attach<broker::master>(name, type, opts);
	    })
    .def("attach",
         [](broker::endpoint& ep, const std::string& name) -> broker::expected<broker::store> {
	        return ep.attach<broker::clone>(name);
	    })
   ;

  return m.ptr();
}

