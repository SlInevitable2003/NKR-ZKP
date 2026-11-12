// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use winterfell::{BatchingMethod, FieldExtension, ProofOptions};

use super::{hash, BaseElement, Blake3_256, FieldElement, PoseidonExample};
use crate::density::Density;

fn build_options() -> ProofOptions {
    ProofOptions::new(
        28,
        8,
        0,
        FieldExtension::None,
        4,
        31,
        BatchingMethod::Algebraic,
        BatchingMethod::Algebraic,
    )
}

fn build_example(count: usize, density: Density) -> PoseidonExample<Blake3_256> {
    let inputs = density.generate(2 * count);
    let digests = (0..count)
        .map(|index| hash::hash2(inputs[2 * index], inputs[2 * index + 1]))
        .collect();
    PoseidonExample::new(count, build_options(), false, inputs, digests)
}

#[test]
fn poseidon_test_basic_proof_verification() {
    crate::tests::test_basic_proof_verification(Box::new(build_example(1, Density::Full)));
}

#[test]
fn poseidon_test_basic_proof_verification_fail() {
    crate::tests::test_basic_proof_verification_fail(Box::new(build_example(2, Density::Full)));
}

#[test]
fn poseidon_density_levels_bind_all_inputs() {
    use crate::Example;

    for density in [Density::Full, Density::Hybrid, Density::Sparse] {
        let example = build_example(2, density);
        let zeros = example.inputs.iter().filter(|&&value| value == BaseElement::ZERO).count();
        assert_eq!(example.inputs.len() - zeros, density.nonzero_count(4));

        let proof = example.prove();
        assert!(example.verify(proof.clone()).is_ok());
        assert!(example.verify_with_wrong_inputs(proof).is_err());
    }
}
