#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <math.h>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include "../tensors.hpp"
#include "pde_base.hpp"

// ---------------------------------------------------------------------------
// Full PDE from the 2D runaway electron paper
//
// d/dt f(p,z) == -div(flux_C + flux_E + flux_R)
//
// where
//
// flux_C is flux due to electron-ion collisions
// flux_E is the flux due to E accleration
// flux_R is the flux due to radiation damping
//
// -div(flux_C) == termC1 + termC2 + termC3
//
// termC1 == 1/p^2*d/dp*p^2*Ca*df/dp
// termC2 == 1/p^2*d/dp*p^2*Cf*f
// termC3 == termC3 == Cb(p)/p^4 * d/dz( (1-z^2) * df/dz )
//
// -div(flux_E) == termE1 + termE2
//
// termE1 == -E*z*f(z) * 1/p^2 (d/dp p^2 f(p))
// termE2 == -E*p*f(p) * d/dz (1-z^2) f(z)
//
// -div(flux_R) == termR1 + termR2
//
// termR1 == 1/p^2 d/dp p^2 gamma(p) p / tau f(p) * (1-z^2) * f(z)
// termR2 == -1/(tau*gam(p)) f(p) * d/dz z(1-z^2) f(z)
// ---------------------------------------------------------------------------

template<typename P>
class PDE_impurity_3d_A : public PDE<P>
{
public:
  PDE_impurity_3d_A(int const num_levels = -1, int const degree = -1)
      : PDE<P>(num_levels, degree, num_dims_, num_sources_, num_terms_,
               dimensions_, terms_, sources_, exact_vector_funcs_,
               exact_scalar_func_, get_dt_, do_poisson_solve_,
               has_analytic_soln_)
  {}

private:
  // these fields will be checked against provided functions to make sure
  // everything is specified correctly

  static int constexpr num_dims_           = 3;
  static int constexpr num_sources_        = 0;
  static int constexpr num_terms_          = 8;
  static bool constexpr do_poisson_solve_  = false;
  static bool constexpr has_analytic_soln_ = false;

  // ------------------------------------------------
  //
  // define constants / data / functions for this PDE
  //
  // ------------------------------------------------

  static auto constexpr phi = [](P x) { return std::erf(x); };

  static auto constexpr psi = [](P x) {
    auto dphi_dx = 2.0 / std::sqrt(M_PI) * std::exp(-std::pow(x, 2));
    auto ret     = 1.0 / (2 * std::pow(x, 2)) * (phi(x) - x * dphi_dx);
    if (x < 1e-5)
      ret = 0;
    return ret;
  };

  static P constexpr nuEE     = 1;
  static P constexpr vT       = 1;
  static P constexpr delta    = 0.3;
  static P constexpr Z        = 5;
  static P constexpr E        = 0.0;
  static P constexpr tau      = 1e5;
  static auto constexpr gamma = [](P p) {
    return std::sqrt(1 + std::pow(delta * p, 2));
  };
  static auto constexpr vx = [](P p) { return 1.0 / vT * (p / gamma(p)); };

  static auto constexpr Ca = [](P p) {
    return nuEE * std::pow(vT, 2) * (psi(vx(p)) / vx(p));
  };

  static auto constexpr Cb = [](P p) {
    return 1.0 / 2.0 * nuEE * std::pow(vT, 2) * 1.0 / vx(p) *
           (Z + phi(vx(p)) - psi(vx(p)) +
            std::pow(delta, 4) * std::pow(vx(p), 2) / 2.0);
  };

  static auto constexpr Cf = [](P p) { return 2.0 * nuEE * vT * psi(vx(p)); };

  // -----------------
  //
  // define dimensions
  //
  // -----------------

  // specify initial conditions for each dim
  // initial condition in s
  static fk::vector<P> initial_condition_s(fk::vector<P> const x, P const t = 0)
  {
    ignore(t);
    fk::vector<P> ret(x.size());

    std::transform(x.begin(), x.end(), ret.begin(), [](auto const elem) {
      P const a = 1;
      if (elem > 0.1 || elemc < 0.2)
      {
        return a;
      }
      else {
        return 0.0;
      }
    });

    return ret;
  }
  // p dimension

  // initial condition in p
  static fk::vector<P> initial_condition_p(fk::vector<P> const x, P const t = 0)
  {
    ignore(t);
    fk::vector<P> ret(x.size());

    std::transform(x.begin(), x.end(), ret.begin(), [](auto const elem) {
      P const a = 2;
      return 2.0 / (std::sqrt(M_PI) * std::pow(a, 3)) *
             std::exp(-std::pow(elem, 2) / std::pow(a, 2));
    });

    return ret;
  }

  // initial conditionn in z
  static fk::vector<P> initial_condition_z(fk::vector<P> const x, P const t = 0)
  {
    ignore(t);
    fk::vector<P> ret(x.size());
    std::fill(ret.begin(), ret.end(), 1.0);
    return ret;
  }

  // p dimension
  inline static dimension<P> const dim_p =
      dimension<P>(0.0,                 // domain min
                   10.0,                // domain max
                   2,                   // levels
                   2,                   // degree
                   initial_condition_p, // initial condition
                   "p");                // name

  // z dimension
  inline static dimension<P> const dim_z =
      dimension<P>(-1.0,                // domain min
                   +1.0,                // domain max
                   2,                   // levels
                   2,                   // degree
                   initial_condition_z, // initial condition
                   "z");                // name
  
  // s dimension
  inline static dimension<P> const dim_s =
      dimension<P>(0.0,                 // domain min
                   1.2,                // domain max
                   2,                   // levels
                   2,                   // degree
                   initial_condition_s, // initial condition
                   "s");                // name

  // assemble dimensions

  inline static std::vector<dimension<P>> const dimensions_ = {dim_p, dim_z, dim_s};

  // ----------------------------------------
  //
  // Setup the terms of the PDE
  //
  // -div(flux_C) == termC1 + termC2 + termC3
  //
  // ----------------------------------------

  // create a default mass matrix
  static P gI(P const x, P const time = 0)
  {
    ignore(x);
    ignore(time);
    return 1.0;
  }

  inline static partial_term<P> const pterm_I =
      partial_term<P>(coefficient_type::mass, gI);
  inline static term<P> const I_ =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "massY",         // name
              dim_p,           // owning dim
              {pterm_I});

  // termC1 == 1/p^2*d/dp*p^2*Ca*df/dp
  //
  //
  // becomes
  //
  // termC1 == g1(p) q(p)        [mass, g1(p) = 1/p^2,  BC N/A]
  //   q(p) == d/dp g2(p) r(p)   [grad, g2(p) = p^2*Ca, BCL=D,BCR=N]
  //   r(p) == d/dp g3(p) f(p)   [grad, g3(p) = 1,      BCL=N,BCR=D]

  static P c1_g1(P const x, P const time = 0)
  {
    ignore(time);
    return 1.0 / std::pow(x, 2);
  }
  static P c1_g2(P const x, P const time = 0)
  {
    ignore(time);
    return std::pow(x, 2) * Ca(x);
  }
  static P c1_g3(P const x, P const time = 0)
  {
    ignore(x);
    ignore(time);
    return 1.0;
  }

  // 1. create partial_terms
  inline static partial_term<P> const c1_pterm1 =
      partial_term<P>(coefficient_type::mass, c1_g1);
  inline static partial_term<P> const c1_pterm2 = partial_term<P>(
      coefficient_type::grad, c1_g2, flux_type::upwind,
      boundary_condition::dirichlet, boundary_condition::neumann);
  inline static partial_term<P> const c1_pterm3 = partial_term<P>(
      coefficient_type::grad, c1_g3, flux_type::downwind,
      boundary_condition::neumann, boundary_condition::dirichlet);

  // 2. combine partial terms into single dimension term
  inline static term<P> const c1_term_p =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "C1_p",          // name
              dim_p,           // owning dim
              {c1_pterm1, c1_pterm2, c1_pterm3});

  // 3. combine single dimension terms into multi dimension term
  inline static std::vector<term<P>> const termC1 = {c1_term_p, I_, I_};

  // termC2 == 1/p^2*d/dp*p^2*Cf*f
  //
  // becomes
  //
  // termC2 == g1(p) q(p)       [mass, g1(p)=1/p^2,  BC N/A]
  //   q(p) == d/dp g2(p) f(p)  [grad, g2(p)=p^2*Cf, BCL=N,BCR=D]

  static P c2_g1(P const x, P const time = 0)
  {
    ignore(time);
    return 1.0 / std::pow(x, 2);
  }
  static P c2_g2(P const x, P const time = 0)
  {
    ignore(time);
    return std::pow(x, 2) * Cf(x);
  }

  // 1. create partial_terms
  inline static partial_term<P> const c2_pterm1 =
      partial_term<P>(coefficient_type::mass, c2_g1);
  inline static partial_term<P> const c2_pterm2 = partial_term<P>(
      coefficient_type::grad, c2_g2, flux_type::upwind,
      boundary_condition::neumann, boundary_condition::dirichlet);

  // 2. combine partial terms into single dimension term
  inline static term<P> const c2_term_p =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "C2_p",          // name
              dim_p,           // owning dim
              {c2_pterm1, c2_pterm2});

  // 3. combine single dimension terms into multi dimension term
  inline static std::vector<term<P>> const termC2 = {c2_term_p, I_, I_};

  // termC3 == Cb(p)/p^4 * d/dz( (1-z^2) * df/dz )
  //
  // becomes
  //
  // termC3 == q(p) r(z)
  // q(p) == g1(p)            [mass, g1(p) = Cb(p)/p^4, BC N/A]
  // r(z) == d/dz g2(z) s(z)  [grad, g2(z) = 1-z^2,     BCL=D,BCR=D]
  // s(z) == d/dz g3(z) f(z)  [grad, g3(z) = 1,         BCL=N,BCR=N]

  static P c3_g1(P const x, P const time = 0)
  {
    ignore(time);
    return Cb(x) / std::pow(x, 4);
  }
  static P c3_g2(P const x, P const time = 0)
  {
    ignore(time);
    return 1.0 - std::pow(x, 2);
  }
  static P c3_g3(P const x, P const time = 0)
  {
    ignore(x);
    ignore(time);
    return 1.0;
  }

  // 1. create partial_terms
  inline static partial_term<P> const c3_pterm1 =
      partial_term<P>(coefficient_type::mass, c3_g1);
  inline static partial_term<P> const c3_pterm2 = partial_term<P>(
      coefficient_type::grad, c3_g2, flux_type::upwind,
      boundary_condition::dirichlet, boundary_condition::dirichlet);
  inline static partial_term<P> const c3_pterm3 =
      partial_term<P>(coefficient_type::grad, c3_g3, flux_type::downwind,
                      boundary_condition::neumann, boundary_condition::neumann);

  // 2. combine partial terms into single dimension term
  inline static term<P> const c3_term_p =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "C3_p",          // name
              dim_p,           // owning dim
              {c3_pterm1});
  inline static term<P> const c3_term_z =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "C3_z",          // name
              dim_p,           // owning dim
              {c3_pterm2, c3_pterm3});

  // 3. combine single dimension terms into multi dimension term
  inline static std::vector<term<P>> const termC3 = {c3_term_p, c3_term_z, I_};

  // -div(flux_E) == termE1 + termE2

  // termE1 == -E*z*f(z) * 1/p^2 (d/dp p^2 f(p))
  //        == r(z) * q(p)
  //   r(z) == g1(z) f(z)       [mass, g1(z) = -E*z,  BC N/A]
  //   q(p) == g2(p) u(p)       [mass, g2(p) = 1/p^2, BC N/A]
  //   u(p) == d/dp g3(p) f(p)  [grad, g3(p) = p^2,   BCL=N,BCR=D]

  static P e1_g1(P const x, P const time = 0)
  {
    ignore(time);
    return -E * x;
  }
  static P e1_g2(P const x, P const time = 0)
  {
    ignore(time);
    assert(x > 0);
    return 1.0 / std::pow(x, 2);
  }
  static P e1_g3(P const x, P const time = 0)
  {
    ignore(time);
    return std::pow(x, 2);
  }

  // 1. create partial_terms
  inline static partial_term<P> const e1_pterm1 =
      partial_term<P>(coefficient_type::mass, e1_g1);
  inline static partial_term<P> const e1_pterm2 =
      partial_term<P>(coefficient_type::mass, e1_g2);
  inline static partial_term<P> const e1_pterm3 = partial_term<P>(
      coefficient_type::grad, e1_g3, flux_type::upwind,
      boundary_condition::neumann, boundary_condition::dirichlet);

  // 2. combine partial terms into single dimension term
  inline static term<P> const e1_term_p =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "E1_p",          // name
              dim_p,           // owning dim
              {e1_pterm2, e1_pterm3});
  inline static term<P> const e1_term_z =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "E1_z",          // name
              dim_p,           // owning dim
              {e1_pterm1});

  // 3. combine single dimension terms into multi dimension term
  inline static std::vector<term<P>> const termE1 = {e1_term_p, e1_term_z, I_};

  // termE2 == -E*p*f(p) * d/dz (1-z^2) f(z)
  //        == q(p) * r(z)
  //   q(p) == g1(p) f(p)       [mass, g1(p) = -E*p,  BC N/A]
  //   r(z) == d/dz g2(z) f(z)  [grad, g2(z) = 1-z^2, BCL=N,BCR=N]

  static P e2_g1(P const x, P const time = 0)
  {
    ignore(time);
    return -E * x;
  }
  static P e2_g2(P const x, P const time = 0)
  {
    ignore(time);
    return 1.0 - std::pow(x, 2);
  }

  // 1. create partial_terms
  inline static partial_term<P> const e2_pterm1 =
      partial_term<P>(coefficient_type::mass, e2_g1);
  inline static partial_term<P> const e2_pterm2 =
      partial_term<P>(coefficient_type::grad, e2_g2, flux_type::central,
                      boundary_condition::neumann, boundary_condition::neumann);

  // 2. combine partial terms into single dimension term
  inline static term<P> const e2_term_p =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "E2_p",          // name
              dim_p,           // owning dim
              {e2_pterm1});
  inline static term<P> const e2_term_z =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "E2_z",          // name
              dim_p,           // owning dim
              {e2_pterm2});

  // 3. combine single dimension terms into multi dimension term
  inline static std::vector<term<P>> const termE2 = {e2_term_p, e2_term_z, I_};

  // -div(flux_R) == termR1 + termR2

  // clang-format off
  //
  // termR1 == 1/p^2 d/dp p^2 gamma(p) p / tau f(p) * (1-z^2) * f(z)
  //        == q(p) * r(z)
  //   q(p) == g1(p) u(p)       [mass, g1(p) = 1/p^2,                BC N/A]
  //   u(p) == d/dp g2(p) f(p)  [grad, g2(p) = p^3 * gamma(p) / tau, BCL=N,BCR=D] 
  //   r(z) == g3(z) f(z)       [mass, g3(z) = 1-z^2,                BC N/A]
  //
  // clang-format on

  static P r1_g1(P const x, P const time = 0)
  {
    ignore(time);
    return 1.0 / std::pow(x, 2);
  }
  static P r1_g2(P const x, P const time = 0)
  {
    ignore(time);
    return std::pow(x, 3) * gamma(x) / tau;
  }
  static P r1_g3(P const x, P const time = 0)
  {
    ignore(time);
    return 1.0 - std::pow(x, 2);
  }

  // 1. create partial_terms
  inline static partial_term<P> const r1_pterm1 =
      partial_term<P>(coefficient_type::mass, r1_g1);
  inline static partial_term<P> const r1_pterm2 = partial_term<P>(
      coefficient_type::grad, r1_g2, flux_type::upwind,
      boundary_condition::neumann, boundary_condition::dirichlet);
  inline static partial_term<P> const r1_pterm3 =
      partial_term<P>(coefficient_type::mass, r1_g3);

  // 2. combine partial terms into single dimension term
  inline static term<P> const r1_term_p =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "R1_p",          // name
              dim_p,           // owning dim
              {r1_pterm1, r1_pterm2});
  inline static term<P> const r1_term_z =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "R1_z",          // name
              dim_p,           // owning dim
              {r1_pterm3});

  // 3. combine single dimension terms into multi dimension term
  inline static std::vector<term<P>> const termR1 = {r1_term_p, r1_term_z, I_};

  // clang-format off
  //
  // termR2 == -1/(tau*gam(p)) f(p) * d/dz z(1-z^2) f(z)
  //        == q(p) * r(z)
  //   q(p) == g1(p) f(p)       [mass, g1(p) = -1/(tau*gamma(p)),    BC N/A]
  //   r(z) == d/dz g2(z) f(z)  [grad, g2(z) = z(1-z^2),             BCL=N,BCR=N]
  //
  // clang-format on

  static P r2_g1(P const x, P const time = 0)
  {
    ignore(time);
    return -1.0 / (tau * gamma(x));
  }
  static P r2_g2(P const x, P const time = 0)
  {
    ignore(time);
    return x * (1.0 - std::pow(x, 2));
  }

  // 1. create partial_terms
  inline static partial_term<P> const r2_pterm1 =
      partial_term<P>(coefficient_type::mass, r2_g1);
  inline static partial_term<P> const r2_pterm2 =
      partial_term<P>(coefficient_type::grad, r2_g2, flux_type::central,
                      boundary_condition::neumann, boundary_condition::neumann);

  // 2. combine partial terms into single dimension term
  inline static term<P> const r2_term_p =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "R2_p",          // name
              dim_p,           // owning dim
              {r2_pterm1});
  inline static term<P> const r2_term_z =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "R2_z",          // name
              dim_p,           // owning dim
              {r2_pterm2});

  // 3. combine single dimension terms into multi dimension term
  inline static std::vector<term<P>> const termR2 = {r2_term_p, r2_term_z, I_};
  
  // clang-format off
  //
  // term_t == -p*z*d/ds f(p,z,s)
  //        == q(p) * r(z)
  //   q(p) == g1(p) f(p)       [mass, g1(p) = -1/(tau*gamma(p)),    BC N/A]
  //   r(z) == d/dz g2(z) f(z)  [grad, g2(z) = z(1-z^2),             BCL=N,BCR=N]
  //
  // clang-format on

  static P t_g1(P const x, P const time = 0)
  {
    ignore(time);
    return -1.0 / (tau * gamma(x));
  }

  // 1. create partial_terms
  inline static partial_term<P> const t_pterm =
      partial_term<P>(coefficient_type::grad, t_g1, flux_type::central,
                      boundary_condition::neumann, boundary_condition::neumann);

  // 2. combine partial terms into single dimension term
  inline static term<P> const t_term_s =
      term<P>(false,           // time-dependent
              fk::vector<P>(), // additional data vector
              "R2_p",          // name
              dim_s,           // owning dim
              {r2_pterm1});

  // 3. combine single dimension terms into multi dimension term
  inline static std::vector<term<P>> const termR2 = {r2_term_p, r2_term_z, I_};

  // collect all the terms
  inline static term_set<P> const terms_ = {termC1, termC2, termC3, termE1,
                                            termE2, termR1, termR2};

  // --------------
  //
  // define sources
  //
  // --------------

  inline static std::vector<source<P>> const sources_ = {};

  // ------------------
  //
  // get time step (dt)
  //
  // ------------------

  static P get_dt_(dimension<P> const &dim)
  {
    P const x_range = dim.domain_max - dim.domain_min;
    P const dx      = x_range / std::pow(2, dim.get_level());
    P const dt      = dx;
    // this will be scaled by CFL from command line
    return dt;
  }

  // -------------------------------------------------
  //
  // define exact soln functions (unused for this PDE)
  //
  // -------------------------------------------------

  inline static std::vector<vector_func<P>> const exact_vector_funcs_ = {};
  inline static scalar_func<P> const exact_scalar_func_               = {};
};
