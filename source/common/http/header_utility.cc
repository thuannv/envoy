#include "common/http/header_utility.h"

#include "common/common/utility.h"
#include "common/config/rds_json.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Http {

// HeaderMatcher will consist of one of the below two options:
// 1.value (string) and regex (bool)
//   An empty header value allows for matching to be only based on header presence.
//   Regex is an opt-in. Unless explicitly mentioned, the header values will be used for
//   exact string matching.
//   This is now deprecated.
// 2.header_match_specifier which can be any one of exact_match, regex_match or range_match.
//   Absence of these options implies empty header value match based on header presence.
//   a.exact_match: value will be used for exact string matching.
//   b.regex_match: Match will succeed if header value matches the value specified here.
//   c.range_match: Match will succeed if header value lies within the range specified
//     here, using half open interval semantics [start,end).
HeaderUtility::HeaderData::HeaderData(const envoy::api::v2::route::HeaderMatcher& config)
    : name_(config.name()) {
  switch (config.header_match_specifier_case()) {
  case envoy::api::v2::route::HeaderMatcher::kExactMatch:
    header_match_type_ = HeaderMatchType::Value;
    value_ = config.exact_match();
    break;
  case envoy::api::v2::route::HeaderMatcher::kRegexMatch:
    header_match_type_ = HeaderMatchType::Regex;
    regex_pattern_ = RegexUtil::parseRegex(config.regex_match());
    break;
  case envoy::api::v2::route::HeaderMatcher::kRangeMatch:
    header_match_type_ = HeaderMatchType::Range;
    range_.set_start(config.range_match().start());
    range_.set_end(config.range_match().end());
    break;
  case envoy::api::v2::route::HeaderMatcher::HEADER_MATCH_SPECIFIER_NOT_SET:
    FALLTHRU;
  default:
    if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, regex, false)) {
      header_match_type_ = HeaderMatchType::Regex;
      regex_pattern_ = RegexUtil::parseRegex(config.value());
    } else {
      header_match_type_ = HeaderMatchType::Value;
      value_ = config.value();
    }
    break;
  }
}

HeaderUtility::HeaderData::HeaderData(const Json::Object& config)
    : HeaderData([&config] {
        envoy::api::v2::route::HeaderMatcher header_matcher;
        Envoy::Config::RdsJson::translateHeaderMatcher(config, header_matcher);
        return header_matcher;
      }()) {}

bool HeaderUtility::matchHeaders(const Http::HeaderMap& request_headers,
                                 const std::vector<HeaderData>& config_headers) {
  bool matches = true;

  if (!config_headers.empty()) {
    for (const HeaderData& cfg_header_data : config_headers) {
      const Http::HeaderEntry* header = request_headers.get(cfg_header_data.name_);

      if (header == nullptr) {
        matches = false;
        break;
      }

      switch (cfg_header_data.header_match_type_) {
      case HeaderMatchType::Value:
        matches &=
            (cfg_header_data.value_.empty() || header->value() == cfg_header_data.value_.c_str());
        break;

      case HeaderMatchType::Regex:
        matches &= std::regex_match(header->value().c_str(), cfg_header_data.regex_pattern_);
        break;

      case HeaderMatchType::Range: {
        int64_t header_value = 0;
        matches &= StringUtil::atol(header->value().c_str(), header_value, 10) &&
                   header_value >= cfg_header_data.range_.start() &&
                   header_value < cfg_header_data.range_.end();
        break;
      }

      default:
        NOT_REACHED;
      }

      if (!matches) {
        break;
      }
    }
  }

  return matches;
}

} // namespace Http
} // namespace Envoy
