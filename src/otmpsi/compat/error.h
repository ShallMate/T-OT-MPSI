#pragma once

#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace otmpsi_compat {

inline void AppendFormat(std::ostringstream &out, std::string_view text) {
  out << text;
}

template <typename T, typename... Args>
void AppendFormat(std::ostringstream &out, std::string_view text, T &&value,
                  Args &&...args) {
  const auto marker = text.find("{}");
  if (marker == std::string_view::npos) {
    out << text << " " << std::forward<T>(value);
  } else {
    out << text.substr(0, marker) << std::forward<T>(value);
    text.remove_prefix(marker + 2);
  }
  AppendFormat(out, text, std::forward<Args>(args)...);
}

inline std::string EnforceMessage(std::string_view expression) {
  return "check failed: " + std::string(expression);
}

template <typename... Args>
std::string EnforceMessage(std::string_view expression, std::string_view text,
                           Args &&...args) {
  std::ostringstream out;
  out << "check failed (" << expression << "): ";
  AppendFormat(out, text, std::forward<Args>(args)...);
  return out.str();
}

template <typename... Args>
[[noreturn]] void Throw(std::string_view text, Args &&...args) {
  std::ostringstream out;
  AppendFormat(out, text, std::forward<Args>(args)...);
  throw std::runtime_error(out.str());
}

} // namespace otmpsi_compat

#define OTMPSI_ENFORCE(expression, ...)                                      \
  do {                                                                        \
    if (!(expression)) {                                                      \
      throw std::runtime_error(otmpsi_compat::EnforceMessage(                 \
          #expression __VA_OPT__(, ) __VA_ARGS__));                           \
    }                                                                         \
  } while (false)

#define OTMPSI_THROW(...) otmpsi_compat::Throw(__VA_ARGS__)
