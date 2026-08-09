#pragma once

#include <cstddef>

namespace examples::otmpsi {

struct BandOkvsParams {
  size_t item_count = 0;
  size_t fitted_item_count = 0;
  int num_vars = 0;
  int band_length = 0;
  double epsilon = 0.03;
  int statistical_security = 40;
  double fitted_security = 0.0;
};

BandOkvsParams ChooseBandOkvsParams(size_t item_count);
BandOkvsParams ChooseBandOkvsParams(size_t item_count, int statistical_security,
                                    double epsilon);

} // namespace examples::otmpsi
