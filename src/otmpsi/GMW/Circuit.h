#pragma once



















#include "otmpsi/GMW/Defines.h"
#include "otmpsi/GMW/config.h"

#ifdef VOLE_PSI_ENABLE_GMW
#include "cryptoTools/Circuit/BetaCircuit.h"

namespace volePSI {
#ifndef ENABLE_CIRCUITS
static_assert(0, "ENABLE_CIRCUITS not defined in cryptoTools");
#endif

using BetaCircuit = oc::BetaCircuit;
using BetaBundle = oc::BetaBundle;

BetaCircuit isZeroCircuit(u64 bits);
void evaluate(BetaCircuit& parent, const BetaCircuit& cir,
              span<BetaBundle> inputs, span<BetaBundle> outputs);

void isZeroCircuit_Test();

}  

#endif
