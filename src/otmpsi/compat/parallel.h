#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace otmpsi_compat {

void SetNumThreads(int num_threads);
int GetNumThreads();

template <typename Function>
void ParallelFor(int64_t begin, int64_t end, Function &&function) {
  if (end <= begin) {
    return;
  }

  const int configured = std::max(1, GetNumThreads());
#ifdef _OPENMP
  if (configured == 1 || omp_in_parallel()) {
    function(begin, end);
    return;
  }

#pragma omp parallel num_threads(configured)
  {
    const int workers = omp_get_num_threads();
    const int worker = omp_get_thread_num();
    const int64_t size = end - begin;
    const int64_t chunk = (size + workers - 1) / workers;
    const int64_t local_begin = std::min(end, begin + worker * chunk);
    const int64_t local_end = std::min(end, local_begin + chunk);
    if (local_begin < local_end) {
      function(local_begin, local_end);
    }
  }
#else
  (void)configured;
  function(begin, end);
#endif
}

} // namespace otmpsi_compat
