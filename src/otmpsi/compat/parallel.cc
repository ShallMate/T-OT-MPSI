#include "otmpsi/compat/parallel.h"

namespace otmpsi_compat {
namespace {
std::atomic<int> g_num_threads{1};
}

void SetNumThreads(int num_threads) {
  const int value = std::max(1, num_threads);
  g_num_threads.store(value, std::memory_order_relaxed);
#ifdef _OPENMP
  omp_set_dynamic(0);
  omp_set_num_threads(value);
#endif
}

int GetNumThreads() {
  return g_num_threads.load(std::memory_order_relaxed);
}

} // namespace otmpsi_compat
