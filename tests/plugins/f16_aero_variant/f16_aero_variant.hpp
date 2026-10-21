// The planted aero defects of M7b PR 4's oracle (c) (docs/plans/m7b-pr4-f16-symmetry.md, sections 2
// and 10), applied to a BuildUp that aircraft/f16/physical/aero/aero_math.hpp's build_up()
// returned. BuildUp's fields are mutable (the M7b plan critique's N-01 note), so each defect
// changes its fields and then recomposes the sums that read them. No shipped code carries any of
// this.
//
// tests/plugins/f16_aero_variant includes this header, and so does
// tests/physics/nesc/f16_symmetry_smoke_test.cpp, which pins recompose() bitwise to build_up().
#pragma once

#include "../../../aircraft/f16/physical/aero/aero_math.hpp"

namespace astra::test::f16_aero_variant {

namespace aero = astra::aircraft_f16_aero_math;

// cl1, cn1, cl and cn from the BuildUp's own fields, in aero_math.hpp:240-249's expression order.
// On an unmodified BuildUp it gives build_up()'s fields back, bit for bit.
inline void recompose(aero::BuildUp& b) noexcept {
  b.cl1 = b.clt + b.dclda * b.dail + b.dcldr * b.drdr;
  b.cn1 = b.cnt + b.dcnda * b.dail + b.dcndr * b.drdr;
  b.cl = b.cl1 + b.b2v * (b.clp * b.p + b.clr * b.r);
  b.cn = b.cn1 + b.b2v * (b.cnp * b.p + b.cnr * b.r);
}

// The absCl0 sign drop (section 10): clt = absCl0 for every beta, where F16_aero.dml has -absCl0
// for beta < 0 (aero_math.hpp:221).
inline void drop_cl0_sign(aero::BuildUp& b) noexcept {
  b.clt = b.abs_cl0;
  recompose(b);
}

// The doubled asymmetry (C-05; section 10): each of the four signed-beta tables gets its odd part
// once more, T(beta) + (T(beta) - T(-beta))/2, with T(-beta) from the same lookup at -beta.
inline void double_odd_parts(const aero::Inputs& in, aero::BuildUp& b) noexcept {
  const auto doubled = [&in](double at_beta, const auto& table) {
    const double at_minus_beta =
        aero::interp::lookup(aero::kBeta2, aero::kAlpha1, table, -in.beta, in.alpha);
    return at_beta + (at_beta - at_minus_beta) / 2.0;
  };
  b.dclda = doubled(b.dclda, aero::tables::kDclda);
  b.dcldr = doubled(b.dcldr, aero::tables::kDcldr);
  b.dcnda = doubled(b.dcnda, aero::tables::kDcnda);
  b.dcndr = doubled(b.dcndr, aero::tables::kDcndr);
  recompose(b);
}

}  // namespace astra::test::f16_aero_variant
