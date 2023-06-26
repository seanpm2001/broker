#include "broker/convert.hh"

#include "broker/data.hh"
#include "broker/data_view.hh"
#include "broker/endpoint_id.hh"

namespace broker::detail {

bool can_convert_data_to_node(const data& src) {
  if (auto str = get_if<std::string>(src))
    return endpoint_id::can_parse(*str);
  else
    return is<none>(src);
}

bool can_convert_data_to_node(const data_view& src) {
  if(src.is_string())
    return endpoint_id::can_parse(src.to_string());
  return src.is_none();
}

} // namespace broker::detail
