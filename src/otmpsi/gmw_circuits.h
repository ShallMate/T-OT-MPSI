#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

volePSI::BetaCircuit BuildPaperThresholdCircuit(size_t parties);
volePSI::BetaCircuit BuildMembershipThresholdCircuit(size_t parties);
volePSI::BetaCircuit BuildMembershipSelectionThresholdCircuit(size_t parties,
                                                              size_t threshold);
volePSI::BetaCircuit
BuildMembershipMaskedHolderThresholdCircuit(size_t parties, size_t threshold);
volePSI::BetaCircuit BuildArithmeticCountThresholdCircuit(size_t parties);
volePSI::BetaCircuit BuildMaskedCountThresholdCircuit(size_t parties);
volePSI::BetaCircuit BuildMaskedHolderThresholdCircuit(size_t parties);
volePSI::BetaCircuit BuildMaskedCountCompareCircuit(size_t parties);
volePSI::BetaCircuit BuildAdditiveMaskedCountCompareCircuit(size_t parties);

} // namespace examples::otmpsi
