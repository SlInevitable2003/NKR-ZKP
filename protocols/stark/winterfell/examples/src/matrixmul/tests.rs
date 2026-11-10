// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use winterfell::{BatchingMethod, FieldExtension, ProofOptions};

use super::Blake3_256;

#[test]
fn matrixmul_test_basic_proof_verification() {
    let example =
        Box::new(super::MatrixMulExample::<Blake3_256>::new(4, build_options(false), false));
    crate::tests::test_basic_proof_verification(example);
}

#[test]
fn matrixmul_test_basic_proof_verification_extension() {
    let example =
        Box::new(super::MatrixMulExample::<Blake3_256>::new(4, build_options(true), false));
    crate::tests::test_basic_proof_verification(example);
}

#[test]
fn matrixmul_test_basic_proof_verification_fail() {
    let example =
        Box::new(super::MatrixMulExample::<Blake3_256>::new(4, build_options(false), false));
    crate::tests::test_basic_proof_verification_fail(example);
}

fn build_options(use_extension_field: bool) -> ProofOptions {
    let extension = if use_extension_field {
        FieldExtension::Quadratic
    } else {
        FieldExtension::None
    };
    ProofOptions::new(
        28,
        8,
        0,
        extension,
        4,
        31,
        BatchingMethod::Algebraic,
        BatchingMethod::Algebraic,
    )
}

#[test]
fn matrixmul_data_modes_preserve_the_product_and_verify() {
    use winterfell::math::FieldElement;

    use crate::{input_data::DataMode, Example};

    for mode in [DataMode::Dense, DataMode::Sparse, DataMode::Hybrid] {
        let example = super::MatrixMulExample::<Blake3_256>::new_with_data_mode(
            4,
            build_options(false),
            false,
            mode,
        );
        let inputs: Vec<_> = example
            .a
            .iter()
            .zip(&example.b)
            .flat_map(|(&left, &right)| [left, right])
            .collect();
        let expected_zeros = match mode {
            DataMode::Dense => 0,
            DataMode::Sparse => 16,
            DataMode::Hybrid => 8,
        };
        assert_eq!(
            inputs.iter().filter(|&&value| value == super::BaseElement::ZERO).count(),
            expected_zeros
        );
        if mode == DataMode::Hybrid {
            assert!(inputs[..16].iter().all(|&value| value != super::BaseElement::ZERO));
        }
        assert_eq!(example.c, super::multiply(&example.a, &example.b, 4));
        let proof = example.prove();
        assert!(example.verify(proof.clone()).is_ok());
        assert!(example.verify_with_wrong_inputs(proof).is_err());
    }
}
