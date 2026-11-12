// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use winterfell::{BatchingMethod, FieldExtension, ProofOptions};

use super::{BaseElement, Blake3_256, FieldElement, SqlExample};
use crate::density::Density;
use crate::Example;

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

fn build_example(row_count: usize, density: Density) -> SqlExample<Blake3_256> {
    let amounts = density.generate(row_count);
    let mut rows = Vec::with_capacity(row_count);
    for (index, amount) in amounts.into_iter().enumerate() {
        let category = BaseElement::new(((index * 7 + 3) % 16) as u128);
        rows.push((category, amount));
    }
    let target = rows[0].0;
    SqlExample::new(rows, target, build_options(), false)
}

#[test]
fn sql_test_basic_proof_verification() {
    crate::tests::test_basic_proof_verification(Box::new(build_example(2, Density::Full)));
}

#[test]
fn sql_test_basic_proof_verification_fail() {
    crate::tests::test_basic_proof_verification_fail(Box::new(build_example(2, Density::Full)));
}

#[test]
fn sql_density_levels_apply_to_amounts() {
    for density in [Density::Full, Density::Hybrid, Density::Sparse] {
        let example = build_example(4, density);
        let zeros = example.rows.iter().filter(|(_, amount)| *amount == BaseElement::ZERO).count();
        assert_eq!(example.rows.len() - zeros, density.nonzero_count(4));

        let proof = example.prove();
        assert!(example.verify(proof.clone()).is_ok());
        assert!(example.verify_with_wrong_inputs(proof).is_err());
    }
}
