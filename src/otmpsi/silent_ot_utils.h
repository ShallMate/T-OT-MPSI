#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

inline void ConfigureSilentOtSender(oc::SilentOtExtSender &sender,
                                    size_t num_ots, size_t num_threads) {
  sender.configure(static_cast<oc::u64>(num_ots), 2,
                   static_cast<oc::u64>(std::max<size_t>(1, num_threads)),
                   oc::SilentSecType::SemiHonest);
}

inline void ConfigureSilentOtReceiver(oc::SilentOtExtReceiver &receiver,
                                      size_t num_ots, size_t num_threads) {
  receiver.configure(static_cast<oc::u64>(num_ots), 2,
                     static_cast<oc::u64>(std::max<size_t>(1, num_threads)),
                     oc::SilentSecType::SemiHonest);
}

} // namespace examples::otmpsi
