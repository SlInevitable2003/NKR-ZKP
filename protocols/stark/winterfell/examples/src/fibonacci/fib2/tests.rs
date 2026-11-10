// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use super::{super::utils::build_proof_options, Blake3_256};

#[test]
fn fib2_test_basic_proof_verification() {
    let fib = Box::new(super::FibExample::<Blake3_256>::new(16, build_proof_options(false)));
    crate::tests::test_basic_proof_verification(fib);
}

#[test]
fn fib2_test_basic_proof_verification_extension() {
    let fib = Box::new(super::FibExample::<Blake3_256>::new(16, build_proof_options(true)));
    crate::tests::test_basic_proof_verification(fib);
}

#[test]
fn fib2_test_basic_proof_verification_fail() {
    let fib = Box::new(super::FibExample::<Blake3_256>::new(16, build_proof_options(false)));
    crate::tests::test_basic_proof_verification_fail(fib);
}

#[cfg(feature = "std")]
#[test]
fn fibonacci_data_modes_bind_both_seeds_and_the_result() {
    use super::{BaseElement, FibExample, FieldElement};
    use crate::{input_data::DataMode, Example};

    for mode in [DataMode::Dense, DataMode::Sparse, DataMode::Hybrid] {
        let inputs = mode.generate(2);
        let seeds = [inputs[0], inputs[1]];
        assert_eq!(
            inputs.iter().filter(|&&value| value == BaseElement::ZERO).count(),
            if mode == DataMode::Sparse { 1 } else { 0 }
        );
        let mut example =
            FibExample::<Blake3_256>::new_with_inputs(16, build_proof_options(false), seeds, false);
        let mut sequence = seeds.to_vec();
        for index in 2..16 {
            sequence.push(sequence[index - 2] + sequence[index - 1]);
        }
        assert_eq!(example.result, sequence[15]);
        let proof = example.prove();
        assert!(example.verify(proof.clone()).is_ok());
        assert!(example.verify_with_wrong_inputs(proof.clone()).is_err());
        for index in 0..2 {
            example.seeds[index] += BaseElement::ONE;
            assert!(example.verify(proof.clone()).is_err());
            example.seeds[index] = seeds[index];
        }
    }
}
