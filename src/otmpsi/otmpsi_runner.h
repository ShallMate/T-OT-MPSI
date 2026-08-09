#pragma once

#include "otmpsi/b2a_phase.h"
#include "otmpsi/demo_data.h"
#include "otmpsi/tree_x2a_eda_bit.h"
#include "otmpsi/final_reveal_phase.h"
#include "otmpsi/nparty_gmw.h"
#include "otmpsi/pairwise_zero_phase.h"
#include "otmpsi/private_selector_ot.h"
#include "otmpsi/relaxed_selector_open.h"
#include "otmpsi/ssbsmt_phase.h"

namespace examples::otmpsi {

OtmpsiRun RunOnePartyProcessWithStats(size_t party_id,
                                      const OtmpsiParams &params);
OtmpsiRun RunDemoWithStats(const OtmpsiParams &params);
std::vector<OtmpsiResult> RunDemo(const OtmpsiParams &params);

} // namespace examples::otmpsi
