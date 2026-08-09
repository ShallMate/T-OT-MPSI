#include "otmpsi/band_okvs_params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "otmpsi/compat/error.h"

namespace examples::otmpsi {
namespace {

constexpr int kDefaultStatisticalSecurity = 48;
constexpr double kDefaultEpsilon = 0.03;
constexpr int kMaxSupportedBandLength = 768;
constexpr size_t kMinFittedItemCount = size_t{1} << 10;

struct BandOkvsFit {
  int epsilon_hundredths = 0;
  size_t item_count = 0;
  double a = 0.0;
  double b = 0.0;
};

// Appendix F / Figure 17 of ePrint 2023/903 gives best-fit lines
// lambda = a*w + b for RB-OKVS at selected epsilon and n values.
constexpr std::array<BandOkvsFit, 23> kFits = {{
    {3, size_t{1} << 10, 0.08047, -3.464},
    {3, size_t{1} << 14, 0.08253, -5.751},
    {3, size_t{1} << 16, 0.08241, -7.023},
    {3, size_t{1} << 18, 0.08192, -8.569},
    {3, size_t{1} << 20, 0.08313, -10.880},
    {3, size_t{1} << 24, 0.08253, -14.671},
    {5, size_t{1} << 10, 0.1388, -4.424},
    {5, size_t{1} << 14, 0.1389, -6.976},
    {5, size_t{1} << 16, 0.1399, -8.942},
    {5, size_t{1} << 18, 0.1388, -10.710},
    {5, size_t{1} << 20, 0.1407, -12.920},
    {5, size_t{1} << 24, 0.1376, -16.741},
    {7, size_t{1} << 10, 0.1947, -5.383},
    {7, size_t{1} << 14, 0.1926, -8.150},
    {7, size_t{1} << 16, 0.1961, -10.430},
    {7, size_t{1} << 18, 0.1955, -12.300},
    {7, size_t{1} << 20, 0.1939, -14.100},
    {10, size_t{1} << 10, 0.2747, -6.296},
    {10, size_t{1} << 14, 0.2685, -9.339},
    {10, size_t{1} << 16, 0.2740, -11.610},
    {10, size_t{1} << 18, 0.2715, -13.390},
    {10, size_t{1} << 20, 0.2691, -15.210},
    {10, size_t{1} << 24, 0.2751, -19.830},
}};

int ReadEnvInt(const char *name, int fallback) {
  const char *env = std::getenv(name);
  if (env == nullptr || env[0] == '\0') {
    return fallback;
  }
  char *end = nullptr;
  const long value = std::strtol(env, &end, 10);
  OTMPSI_ENFORCE(end != env && *end == '\0' && value > 0 &&
                   value <= std::numeric_limits<int>::max(),
               "{} must be a positive integer, got {}", name, env);
  return static_cast<int>(value);
}

double ReadEnvDouble(const char *name, double fallback) {
  const char *env = std::getenv(name);
  if (env == nullptr || env[0] == '\0') {
    return fallback;
  }
  char *end = nullptr;
  const double value = std::strtod(env, &end);
  OTMPSI_ENFORCE(end != env && *end == '\0' && std::isfinite(value) &&
                   value > 0.0,
               "{} must be a positive decimal, got {}", name, env);
  return value;
}

int EpsilonHundredths(double epsilon) {
  const auto rounded = static_cast<int>(std::lround(epsilon * 100.0));
  OTMPSI_ENFORCE(std::fabs(epsilon - static_cast<double>(rounded) / 100.0) < 1e-9,
               "Band OKVS epsilon must match Appendix F values 0.03, 0.05, "
               "0.07, or 0.10, got {}",
               epsilon);
  return rounded;
}

const BandOkvsFit &FindFit(size_t item_count, int epsilon_hundredths) {
  const BandOkvsFit *selected = nullptr;
  for (const auto &fit : kFits) {
    if (fit.epsilon_hundredths != epsilon_hundredths) {
      continue;
    }
    if (fit.item_count >= item_count &&
        (selected == nullptr || fit.item_count < selected->item_count)) {
      selected = &fit;
    }
  }
  OTMPSI_ENFORCE(selected != nullptr,
               "No Appendix F Band OKVS fit for epsilon={} and n={}; choose "
               "a supported epsilon or n <= the largest fitted table row",
               static_cast<double>(epsilon_hundredths) / 100.0, item_count);
  return *selected;
}

size_t CeilMulOnePlusEpsilon(size_t item_count, int epsilon_hundredths) {
  constexpr size_t kDenominator = 100;
  const size_t numerator =
      kDenominator + static_cast<size_t>(epsilon_hundredths);
  OTMPSI_ENFORCE(item_count <= std::numeric_limits<size_t>::max() / numerator,
               "Band OKVS item count is too large");
  return (item_count * numerator + kDenominator - 1) / kDenominator;
}

} // namespace

BandOkvsParams ChooseBandOkvsParams(size_t item_count) {
  return ChooseBandOkvsParams(
      item_count,
      ReadEnvInt("OTMPSI_BANDOKVS_SECURITY", kDefaultStatisticalSecurity),
      ReadEnvDouble("OTMPSI_BANDOKVS_EPSILON", kDefaultEpsilon));
}

BandOkvsParams ChooseBandOkvsParams(size_t item_count, int statistical_security,
                                    double epsilon) {
  OTMPSI_ENFORCE(item_count >= kMinFittedItemCount,
               "Band OKVS Appendix F fits start at n=1024, got {}", item_count);
  OTMPSI_ENFORCE(statistical_security > 0,
               "Band OKVS statistical security must be positive, got {}",
               statistical_security);
  const int epsilon_hundredths = EpsilonHundredths(epsilon);
  const auto &fit = FindFit(item_count, epsilon_hundredths);
  const int band_length = std::max(
      1, static_cast<int>(std::ceil(
             (static_cast<double>(statistical_security) - fit.b) / fit.a)));
  OTMPSI_ENFORCE(
      band_length <= kMaxSupportedBandLength,
      "Band OKVS Appendix F requires band_length={} for epsilon={} and "
      "lambda={}, but this implementation supports at most {}",
      band_length, epsilon, statistical_security, kMaxSupportedBandLength);
  const size_t num_vars_size =
      std::max(CeilMulOnePlusEpsilon(item_count, epsilon_hundredths),
               static_cast<size_t>(band_length + 1));
  OTMPSI_ENFORCE(num_vars_size <=
                   static_cast<size_t>(std::numeric_limits<int>::max()),
               "Band OKVS table length is too large: {}", num_vars_size);

  BandOkvsParams params;
  params.item_count = item_count;
  params.fitted_item_count = fit.item_count;
  params.num_vars = static_cast<int>(num_vars_size);
  params.band_length = band_length;
  params.epsilon = static_cast<double>(epsilon_hundredths) / 100.0;
  params.statistical_security = statistical_security;
  params.fitted_security = fit.a * static_cast<double>(band_length) + fit.b;
  return params;
}

} // namespace examples::otmpsi
