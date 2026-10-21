#include <iostream>

#include "otmpsi/otmpsi.h"

int main() {
  examples::otmpsi::OtmpsiParams params;
  params.parties = 3;
  params.set_size = 1 << 10;
  params.threshold = 2;
  params.threads = 2;
  params.base_port = 28400;
  params.final_output_mode =
      examples::otmpsi::FinalOutputMode::kClientOutputBasis;

  const auto run = examples::otmpsi::RunDemoWithStats(params);
  std::cout << "output_size=" << run.results.size() << '\n';
  std::cout << "total_time_s=" << run.stats.total_time_s << '\n';
  return run.results.empty() ? 1 : 0;
}
