#ifndef BROKER_DATA_HH
#define BROKER_DATA_HH

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <caf/actor.hpp>
#include <caf/intrusive_ptr.hpp>
#include <caf/ref_counted.hpp>
#include <caf/variant.hpp>

#include <caf/detail/comparable.hpp>

#include "broker/address.hh"
#include "broker/enum_value.hh"
#include "broker/fwd.hh"
#include "broker/none.hh"
#include "broker/optional.hh"
#include "broker/port.hh"
#include "broker/subnet.hh"
#include "broker/time.hh"

#include "broker/detail/hash.hh"
#include "broker/detail/variant.hh"

namespace broker {

class data;

/// A container of sequential data.
using vector = std::vector<data>;

/// @relates vector
bool convert(const vector& v, std::string& str);

/// An associative, ordered container of unique keys.
using set = std::set<data>;

/// @relates set
bool convert(const set& s, std::string& str);

/// An associative, ordered container that maps unique keys to values.
using table = std::map<data, data>;

/// @relates table
bool convert(const table& t, std::string& str);

using data_variant = detail::variant<
  none,
  boolean,
  count,
  integer,
  real,
  std::string,
  address,
  subnet,
  port,
  timestamp,
  timespan,
  enum_value,
  set,
  table,
  vector
>;

/// A variant class that may store the data associated with one of several
/// different primitive or compound types.
class data : public data_variant {
public:
    enum class type : uint8_t {
      address,
      boolean,
      count,
      enum_value,
      integer,
      none,
      port,
      real,
      set,
      string,
      subnet,
      table,
      timespan,
      timestamp,
      vector
    };
		
	template <class T>
	using from = detail::conditional_t<
        std::is_floating_point<T>::value,
        real,
        detail::conditional_t<
          std::is_same<T, bool>::value,
          boolean,
          detail::conditional_t<
            std::is_unsigned<T>::value,
            count,
            detail::conditional_t<
              std::is_signed<T>::value,
              integer,
              detail::conditional_t<
                std::is_convertible<T, std::string>::value,
                std::string,
                detail::conditional_t<
                  std::is_same<T, timestamp>::value
                    || std::is_same<T, timespan>::value
                    || std::is_same<T, enum_value>::value
	                  || std::is_same<T, address>::value
                    || std::is_same<T, subnet>::value
                    || std::is_same<T, port>::value
                    || std::is_same<T, set>::value
                    || std::is_same<T, table>::value
                    || std::is_same<T, vector>::value,
                  T,
                  std::false_type
                >
              >
            >
          >
        >
      >;

  /// Default-constructs an empty data value in `none` state.
  data(none = nil) {
    // nop
  }

  /// Constructs a data value from one of the possible data types.
	template <
	  class T,
	  class = detail::disable_if_t<
              detail::is_same_or_derived<data, T>::value
                || std::is_same<
                     from<detail::decay_t<T>>,
                     std::false_type
                   >::value
            >
	>
	data(T&& x) : data_variant(from<detail::decay_t<T>>(std::forward<T>(x))) {
	  // nop
	}

  /// Returns a string representation of the stored type.
  type get_type() const;
};

// C++17 variant compliance.
using detail::get;
using detail::get_if;
using detail::is;

/// Perform multiple dispatch on data instances.
/// @tparam Visitor The visitor type.
/// @param visitor The visitor to instance.
/// @param xs The data instances that *visitor* should visit.
/// @returns The return value of `Visitor::operator()`.
/// @relates data
template <class Visitor, class... Ts>
auto visit(Visitor&& visitor, Ts&&... xs)
-> detail::enable_if_t<
  detail::are_same<data, detail::decay_t<Ts>...>::value,
  typename Visitor::result_type
> {
  return detail::visit(std::forward<Visitor>(visitor),
                       std::forward<Ts>(xs)...);
}

/// @relates data
bool convert(const data& d, std::string& str);

} // namespace broker

// --- implementations of std::hash --------------------------------------------

namespace std {

template <>
struct hash<broker::data> {
  using result_type = typename std::hash<broker::data_variant>::result_type;

  inline result_type operator()(const broker::data& d) const {
    return std::hash<broker::data_variant>{}(d);
  }
};

template <>
struct hash<broker::set>
  : broker::detail::container_hasher<broker::set> {};

template <>
struct hash<broker::vector>
  : broker::detail::container_hasher<broker::vector> {};

template <>
struct hash<broker::table::value_type> {
  using result_type = typename std::hash<broker::data>::result_type;

  inline result_type operator()(const broker::table::value_type& p) const {
    auto result = result_type{0};
    broker::detail::hash_combine(result, p.first);
    broker::detail::hash_combine(result, p.second);
    return result;
  }
};

template <>
struct hash<broker::table>
  : broker::detail::container_hasher<broker::table> {};

} // namespace std

#endif // BROKER_DATA_HH
