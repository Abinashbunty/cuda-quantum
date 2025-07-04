/*******************************************************************************
 * Copyright (c) 2022 - 2025 NVIDIA Corporation & Affiliates.                  *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#include "CUDAQTestUtils.h"
#include <cudaq/algorithm.h>

// Rotational gates not supported in Stim.
#ifndef CUDAQ_BACKEND_STIM

struct deuteron_n3_ansatz {
  void operator()(double x0, double x1) __qpu__ {
    cudaq::qvector q(3);
    x(q[0]);
    ry(x0, q[1]);
    ry(x1, q[2]);
    x<cudaq::ctrl>(q[2], q[0]);
    x<cudaq::ctrl>(q[0], q[1]);
    ry(-x0, q[1]);
    x<cudaq::ctrl>(q[0], q[1]);
    x<cudaq::ctrl>(q[1], q[0]);
  }
};

CUDAQ_TEST(ObserveResult, checkSimple) {

  cudaq::spin_op h =
      5.907 - 2.1433 * cudaq::spin_op::x(0) * cudaq::spin_op::x(1) -
      2.1433 * cudaq::spin_op::y(0) * cudaq::spin_op::y(1) +
      .21829 * cudaq::spin_op::z(0) - 6.125 * cudaq::spin_op::z(1);

  auto ansatz = [](double theta) __qpu__ {
    cudaq::qubit q, r;
    x(q);
    ry(theta, r);
    x<cudaq::ctrl>(r, q);
  };

  double result = cudaq::observe(ansatz, h, 0.59);
  EXPECT_NEAR(result, -1.7487, 1e-3);
  printf("Get energy directly as double %lf\n", result);

  auto obs_res = cudaq::observe(ansatz, h, 0.59);
  EXPECT_NEAR(obs_res.expectation(), -1.7487, 1e-3);
  printf("Energy from observe_result %lf\n", obs_res.expectation());

  // Observe using options w/ noise model. Note that the noise model is only
  // honored when using the Density Matrix backend.
  int shots = 252;
  cudaq::set_random_seed(13);
  cudaq::depolarization_channel depol(1.);
  cudaq::noise_model noise;
  noise.add_channel<cudaq::types::x>({0}, depol);
  auto obs_opt =
      cudaq::observe({.shots = shots, .noise = noise}, ansatz, h, 0.59);
  // Verify that the number of shots requested was honored
  auto tmpCounts = obs_opt.raw_data();
  for (auto spinOpName : tmpCounts.register_names()) {
    if (spinOpName == cudaq::GlobalRegisterName)
      continue; // Ignore the global register
    std::size_t totalShots = 0;
    for (auto &[bitstr, counts] : tmpCounts.to_map(spinOpName))
      totalShots += counts;
    EXPECT_EQ(totalShots, shots);
  }

  printf("\n\nLAST ONE!\n");
  auto obs_res2 = cudaq::observe(100000, ansatz, h, 0.59);
  EXPECT_NEAR(obs_res2.expectation(), -1.7, 1e-1);
  printf("Energy from observe_result with shots %lf\n", obs_res2.expectation());
  obs_res2.dump();

  for (const auto &term : h)
    if (!term.is_identity())
      printf("Fine-grain data access: %s = %lf\n", term.to_string().data(),
             obs_res2.expectation(term));

  auto observable = cudaq::spin_op::x(0) * cudaq::spin_op::x(1);
  auto x0x1Counts = obs_res2.counts(observable);
  x0x1Counts.dump();
  EXPECT_TRUE(x0x1Counts.size() == 4);
}

// By default, tensornet backends only compute the overall expectation value in
// observe, i.e., no sub-term calculations.
#ifndef CUDAQ_BACKEND_TENSORNET
CUDAQ_TEST(ObserveResult, checkExpValBug) {

  auto kernel = []() __qpu__ {
    cudaq::qvector qubits(3);
    rx(0.531, qubits[0]);
    ry(0.9, qubits[1]);
    rx(0.3, qubits[2]);
    cz(qubits[0], qubits[1]);
    ry(-0.4, qubits[0]);
    cz(qubits[1], qubits[2]);
  };

  auto hamiltonian = cudaq::spin_op::z(0) + cudaq::spin_op::z(1);

  auto result = cudaq::observe(kernel, hamiltonian);
  auto observable = cudaq::spin_op::z(0);
  auto exp = result.expectation(observable);
  printf("exp %lf \n", exp);
  EXPECT_NEAR(exp, .79, 1e-1);

  observable = cudaq::spin_op::z(1);
  exp = result.expectation(observable);
  printf("exp %lf \n", exp);
  EXPECT_NEAR(exp, .62, 1e-1);

  // We support retrieval of terms as long as they are equal to the
  // terms defined in the spin op passed to observe. A term/operator
  // that acts on two degrees is never the same as an operator that
  // acts on one degree, even if it only acts with an identity on the
  // second degree. While the expectation values generally should be
  // the same in this case, the operators are not (e.g. the respective
  // kernels/gates defined by the two operators is not the same since
  // it acts on a different number of qubits). This is in particular
  // also relevant for noise modeling.
}
#endif

CUDAQ_TEST(ObserveResult, checkObserveWithIdentity) {

  auto kernel = []() __qpu__ {
    cudaq::qvector qubits(5);
    cudaq::exp_pauli(1.0, qubits, "XXIIX");
  };

  const std::string pauliWord = "ZZIIZ";
  const std::size_t numQubits = pauliWord.size();
  auto pauliOp = cudaq::spin_op::from_word(pauliWord);
  // The canonicalized degree list is less than the number of qubits
  EXPECT_LT(cudaq::spin_op::canonicalize(pauliOp).degrees().size(), numQubits);
  auto expVal = cudaq::observe(kernel, pauliOp);
  std::cout << "<" << pauliWord << "> = " << expVal.expectation() << "\n";
  EXPECT_NEAR(expVal.expectation(), -0.416147, 1e-6);
}

#ifdef CUDAQ_BACKEND_TENSORNET
CUDAQ_TEST(ObserveResult, checkObserveWithIdentityLarge) {

  auto kernel = []() __qpu__ {
    cudaq::qvector qubits(50);
    cudaq::exp_pauli(1.0, qubits,
                     "XXIIXXXIIXXXIIXXXIIXXXIIXXXIIXXXIIXXXIIXXXIIXXXIXX");
  };

  const std::string pauliWord =
      "ZZIIZZZIIZZZIIZZZIIZZZIIZZZIIZZZIIZZZIIZZZIIZZZIZZ";
  const std::size_t numQubits = pauliWord.size();
  auto pauliOp = cudaq::spin_op::from_word(pauliWord);
  // The canonicalized degree list is less than the number of qubits
  EXPECT_LT(cudaq::spin_op::canonicalize(pauliOp).degrees().size(), numQubits);
  auto expVal = cudaq::observe(kernel, pauliOp);
  std::cout << "<" << pauliWord << "> = " << expVal.expectation() << "\n";
  EXPECT_NEAR(expVal.expectation(), -0.416147, 1e-3);
}
#endif
#endif
