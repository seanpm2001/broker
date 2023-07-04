#pragma once

#include "broker/address.hh"
#include "broker/detail/promote.hh"
#include "broker/detail/type_traits.hh"
#include "broker/enum_value.hh"
#include "broker/format/bin.hh"
#include "broker/fwd.hh"
#include "broker/none.hh"
#include "broker/port.hh"
#include "broker/subnet.hh"
#include "broker/time.hh"

#include <cstddef>
#include <iterator>
#include <string_view>
#include <tuple>
#include <vector>

namespace broker {

using builder_buffer = std::vector<std::byte>;

class set_builder;
class table_builder;
class list_builder;

// -- is_builder ---------------------------------------------------------------

template <class T>
struct is_builder_oracle : std::false_type {};

template <>
struct is_builder_oracle<set_builder> : std::true_type {};

template <>
struct is_builder_oracle<table_builder> : std::true_type {};

template <>
struct is_builder_oracle<list_builder> : std::true_type {};

template <class T>
inline constexpr bool is_builder = is_builder_oracle<T>::value;

} // namespace broker

namespace broker::detail {

struct builder_access {
  template <class Builder, class T>
  static Builder& add(Builder& builder, T&& value) {
    if constexpr (is_builder<std::decay_t<T>>) {
      auto [first, last] = value.encoded_values();
      format::bin::v1::write_sequence(value.tag(), value.num_values(), first,
                                      last, builder.adder());
    } else {
      format::bin::v1::encode(std::forward<T>(value), builder.adder());
    }
    return builder;
  }
};

} // namespace broker::detail

namespace broker {

// -- set_builder --------------------------------------------------------------

/// A builder for constructing sets.
class set_builder {
public:
  // -- friend types -----------------------------------------------------------

  friend struct detail::builder_access;

  // -- constructors, destructors, and assignment operators --------------------

  set_builder();

  set_builder(set_builder&&) noexcept = default;

  set_builder(const set_builder&) = default;

  set_builder& operator=(set_builder&&) noexcept = default;

  set_builder& operator=(const set_builder&) = default;

  // -- properties -------------------------------------------------------------

  /// The type of the sequence this builder is constructing.
  static constexpr data::type tag() noexcept {
    return data::type::set;
  }

  /// Returns the number of elements in the sequence.
  size_t num_values() const noexcept {
    return size_;
  }

  /// Returns the values in the builder as encoded bytes.
  std::pair<const std::byte*, const std::byte*>
  encoded_values() const noexcept {
    return format::bin::v1::encoded_values(bytes_);
  }

  // -- adders ----------------------------------------------------------------

  template <class T>
  set_builder& add(T&& value) & {
    auto&& pval = detail::promote<T>(value);
    using val_t = std::decay_t<decltype(pval)>;
    static_assert(variant_data::is_primitive<val_t> || is_builder<val_t>,
                  "value must be a valid data type or a builder");
    ++size_;
    return detail::builder_access::add(*this, pval);
  }

  template <class T>
  set_builder&& add(T&& value) && {
    return std::move(add(std::forward<T>(value)));
  }

  /// Adds all elements as a nested vector.
  template <class... Ts>
  set_builder& add_list(Ts&&... xs) & {
    start_inline_vector(sizeof...(xs));
    (detail::builder_access::add(*this, detail::promote<Ts>(xs)), ...);
    return *this;
  }

  template <class... Ts>
  set_builder&& add_list(Ts&&... xs) && {
    return std::move(add_list(std::forward<Ts>(xs)...));
  }

  /// Adds all elements as a nested set.
  /// @pre The elements must be unique.
  template <class... Ts>
  set_builder& add_set(Ts&&... xs) & {
    start_inline_set(sizeof...(xs));
    (detail::builder_access::add(*this, detail::promote<Ts>(xs)), ...);
    return *this;
  }

  template <class... Ts>
  set_builder&& add_set(Ts&&... xs) && {
    return std::move(add_set(std::forward<Ts>(xs)...));
  }

  // -- modifiers --------------------------------------------------------------

  /// Writes meta data to the internal buffer and returns the bytes that the
  /// builder would use when calling `build`.
  std::pair<const std::byte*, size_t> bytes();

  /// Converts the sequence into a @ref variant. The builder becomes invalid
  /// after calling this function.
  variant build() &&;

protected:
  auto adder() {
    return std::back_inserter(bytes_);
  }

  void start_inline_vector(size_t num_elements) {
    ++size_;
    auto out = format::bin::v1::write_unsigned(data::type::vector, adder());
    format::bin::v1::write_varbyte(num_elements, out);
  }

  void start_inline_set(size_t num_elements) {
    ++size_;
    auto out = format::bin::v1::write_unsigned(data::type::set, adder());
    format::bin::v1::write_varbyte(num_elements, out);
  }

  size_t size_ = 0;
  builder_buffer bytes_;
};

// -- table_builder ------------------------------------------------------------

/// A builder for constructing vectors.
class table_builder {
public:
  // -- friend types -----------------------------------------------------------

  friend struct detail::builder_access;

  // -- constructors, destructors, and assignment operators --------------------

  table_builder();

  table_builder(table_builder&&) noexcept = default;

  table_builder(const table_builder&) = default;

  table_builder& operator=(table_builder&&) noexcept = default;

  table_builder& operator=(const table_builder&) = default;

  // -- properties -------------------------------------------------------------

  /// The type of the sequence this builder is constructing.
  static constexpr data::type tag() noexcept {
    return data::type::table;
  }

  /// Returns the number of elements in the sequence.
  size_t num_values() const noexcept {
    return size_;
  }

  /// Returns the values in the builder as encoded bytes.
  std::pair<const std::byte*, const std::byte*>
  encoded_values() const noexcept {
    return format::bin::v1::encoded_values(bytes_);
  }

  // -- adders ----------------------------------------------------------------

  template <class Key, class Val>
  table_builder& add(Key&& key_arg, Val&& val_arg) & {
    auto&& key = detail::promote<Key>(key_arg);
    auto&& val = detail::promote<Val>(val_arg);
    using key_t = std::decay_t<decltype(key)>;
    using val_t = std::decay_t<decltype(val)>;
    static_assert(variant_data::is_primitive<key_t> || is_builder<key_t>,
                  "key must be a valid data type or a builder");
    static_assert(variant_data::is_primitive<val_t> || is_builder<val_t>,
                  "value must be a valid data type or a builder");
    ++size_;
    detail::builder_access::add(*this, key);
    detail::builder_access::add(*this, val);
    return *this;
  }

  // -- rvalue overloads -------------------------------------------------------

  template <class Key, class Value>
  table_builder&& add(Key&& key, Value&& value) && {
    return std::move(add(std::forward<Key>(key), std::forward<Value>(value)));
  }

  // -- modifiers --------------------------------------------------------------

  /// Writes meta data to the internal buffer and returns the bytes that the
  /// builder would use when calling `build`.
  std::pair<const std::byte*, size_t> bytes();

  /// Converts the sequence into a @ref variant. The builder becomes invalid
  /// after calling this function.
  variant build() &&;

protected:
  auto adder() {
    return std::back_inserter(bytes_);
  }

  size_t size_ = 0;
  builder_buffer bytes_;
};

/// A builder for constructing vectors.
class list_builder {
public:
  // -- friend types -----------------------------------------------------------

  friend struct detail::builder_access;

  // -- constructors, destructors, and assignment operators --------------------

  list_builder();

  list_builder(list_builder&&) noexcept = default;

  list_builder(const list_builder&) = default;

  list_builder& operator=(list_builder&&) noexcept = default;

  list_builder& operator=(const list_builder&) = default;

  // -- properties -------------------------------------------------------------

  /// The type of the sequence this builder is constructing.
  static constexpr data::type tag() noexcept {
    return data::type::vector;
  }

  /// Returns the number of elements in the sequence.
  size_t num_values() const noexcept {
    return size_;
  }

  /// Returns the values in the builder as encoded bytes.
  std::pair<const std::byte*, const std::byte*>
  encoded_values() const noexcept {
    return format::bin::v1::encoded_values(bytes_);
  }

  // -- adders ----------------------------------------------------------------

  template <class T>
  list_builder& add(T&& value) & {
    auto&& pval = detail::promote<T>(value);
    using val_t = std::decay_t<decltype(pval)>;
    if constexpr (detail::is_tuple<val_t>) {
      std::apply([&](auto&&... xs) { (add_list(xs), ...); }, pval);
      return *this;
    } else {
      static_assert(variant_data::is_primitive<val_t> || is_builder<val_t>,
                    "value must be a valid data type or a builder");
      ++size_;
      return detail::builder_access::add(*this, pval);
    }
  }

  template <class T>
  list_builder&& add(T&& value) && {
    return std::move(add(std::forward<T>(value)));
  }

  /// Adds all elements as a nested vector.
  template <class... Ts>
  list_builder& add_list(Ts&&... xs) & {
    start_inline_vector(sizeof...(xs));
    (add_inline_vector_item(std::forward<Ts>(xs)), ...);
    return *this;
  }

  template <class... Ts>
  list_builder&& add_list(Ts&&... xs) && {
    return std::move(add_list(std::forward<Ts>(xs)...));
  }

  /// Adds all elements as a nested set.
  /// @pre The elements must be unique.
  template <class... Ts>
  list_builder& add_set(Ts&&... xs) & {
    start_inline_set(sizeof...(xs));
    (detail::builder_access::add(*this, detail::promote<Ts>(xs)), ...);
    return *this;
  }

  template <class... Ts>
  list_builder&& add_set(Ts&&... xs) && {
    return std::move(add_set(std::forward<Ts>(xs)...));
  }

  // -- modifiers --------------------------------------------------------------

  /// Writes meta data to the internal buffer and returns the bytes that the
  /// builder would use when calling `build`.
  std::pair<const std::byte*, size_t> bytes();

  /// Converts the sequence into a @ref variant. The builder becomes invalid
  /// after calling this function.
  variant build() &&;

protected:
  auto adder() {
    return std::back_inserter(bytes_);
  }

  template <class T>
  void add_inline_vector_item(T&& value) {
    auto&& pval = detail::promote<T>(value);
    using val_t = std::decay_t<decltype(pval)>;
    if constexpr (detail::is_tuple<val_t>) {
      auto out = format::bin::v1::write_unsigned(data::type::vector, adder());
      format::bin::v1::write_varbyte(std::tuple_size<val_t>::value, out);
      std::apply([&](auto&&... xs) { (add_inline_vector_item(xs), ...); },
                 pval);
    } else {
      static_assert(variant_data::is_primitive<val_t> || is_builder<val_t>,
                    "value must be a valid data type or a builder");
      detail::builder_access::add(*this, pval);
    }
  }

  void start_inline_vector(size_t num_elements) {
    ++size_;
    auto out = format::bin::v1::write_unsigned(data::type::vector, adder());
    format::bin::v1::write_varbyte(num_elements, out);
  }

  void start_inline_set(size_t num_elements) {
    ++size_;
    auto out = format::bin::v1::write_unsigned(data::type::set, adder());
    format::bin::v1::write_varbyte(num_elements, out);
  }

  size_t size_ = 0;
  builder_buffer bytes_;
};

} // namespace broker
