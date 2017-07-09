#include "broker/data.hh"
#include "broker/convert.hh"

namespace broker {

struct type_name_getter {
  using result_type = data::type;

  result_type operator()(broker::address) {
    return data::type::address;
  }

  result_type operator()(broker::boolean) {
    return data::type::boolean;
  }

  result_type operator()(broker::count) {
    return data::type::count;
  }

  result_type operator()(broker::enum_value) {
    return data::type::enum_value;
  }

  result_type operator()(broker::integer) {
    return data::type::integer;
  }

  result_type operator()(broker::none) {
    return data::type::none;
  }

  result_type operator()(broker::port) {
    return data::type::port;
  }

  result_type operator()(broker::real) {
    return data::type::real;
  }

  result_type operator()(broker::set) {
    return data::type::set;
  }

  result_type operator()(std::string) {
    return data::type::string;
  }

  result_type operator()(broker::subnet) {
    return data::type::subnet;
  }

  result_type operator()(broker::table) {
    return data::type::table;
  }

  result_type operator()(broker::timespan) {
    return data::type::timespan;
  }

  result_type operator()(broker::timestamp) {
    return data::type::timestamp;
  }

  result_type operator()(broker::vector) {
    return data::type::vector;
  }
};

data::type data::get_type() const {
  return visit(type_name_getter(), *this);
}

namespace {

template <class Container>
void container_convert(Container& c, std::string& str,
                       const char* left, const char* right,
                       const char* delim = ", ") {
  auto first = begin(c);
  auto last = end(c);
  str += left;
  if (first != last) {
    str += to_string(*first);
    while (++first != last)
      str += delim + to_string(*first);
  }
  str += right;
}

struct data_converter {
  using result_type = bool;

  template <class T>
  result_type operator()(const T& x) {
    return convert(x, str);
  }

  result_type operator()(bool b) {
    str = b ? 'T' : 'F';
    return true;
  }

  result_type operator()(const std::string& x) {
    str = x;
    return true;
  }

  std::string& str;
};

} // namespace <anonymous>

bool convert(const table::value_type& e, std::string& str) {
  str += to_string(e.first) + " -> " + to_string(e.second);
  return true;
}

bool convert(const vector& v, std::string& str) {
  container_convert(v, str, "[", "]");
  return true;
}

bool convert(const set& s, std::string& str) {
  container_convert(s, str, "{", "}");
  return true;
}

bool convert(const table& t, std::string& str) {
  container_convert(t, str, "{", "}");
  return true;
}

bool convert(const data& d, std::string& str) {
  visit(data_converter{str}, d);
  return true;
}

} // namespace broker
