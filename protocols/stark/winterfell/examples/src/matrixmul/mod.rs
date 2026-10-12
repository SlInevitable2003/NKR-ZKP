// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use core::marker::PhantomData;
use std::time::Instant;

use rand_utils::rand_vector;
use tracing::{field, info_span};
use winterfell::{
    crypto::{DefaultRandomCoin, ElementHasher, MerkleTree},
    math::{fields::f128::BaseElement, FieldElement},
    Proof, ProofOptions, Prover, Trace, VerifierError,
};

use crate::{Blake3_192, Blake3_256, Example, ExampleOptions, HashFunction, Sha3_256};

mod air;
use air::{MatrixMulAir, PublicInputs};

mod prover;
use prover::MatrixMulProver;

#[cfg(test)]
mod tests;

// CONSTANTS
// ================================================================================================

const TRACE_WIDTH: usize = 4;

// MATRIX MULTIPLICATION EXAMPLE
// ================================================================================================

pub fn get_example(
    options: &ExampleOptions,
    matrix_size: usize,
) -> Result<Box<dyn Example>, String> {
    let (options, hash_fn) = options.to_proof_options(28, 8);

    match hash_fn {
        HashFunction::Blake3_192 => {
            Ok(Box::new(MatrixMulExample::<Blake3_192>::new(matrix_size, options)))
        },
        HashFunction::Blake3_256 => {
            Ok(Box::new(MatrixMulExample::<Blake3_256>::new(matrix_size, options)))
        },
        HashFunction::Sha3_256 => {
            Ok(Box::new(MatrixMulExample::<Sha3_256>::new(matrix_size, options)))
        },
        _ => Err("The specified hash function cannot be used with this example.".to_string()),
    }
}

pub struct MatrixMulExample<H: ElementHasher> {
    options: ProofOptions,
    matrix_size: usize,
    a: Vec<BaseElement>,
    b: Vec<BaseElement>,
    c: Vec<BaseElement>,
    _hasher: PhantomData<H>,
}

impl<H: ElementHasher> MatrixMulExample<H> {
    pub fn new(matrix_size: usize, options: ProofOptions) -> Self {
        assert!(matrix_size.is_power_of_two(), "matrix size must be a power of 2");
        assert!(matrix_size >= 2, "matrix size must be at least 2");

        let input_start = Instant::now();
        let a = build_matrix(matrix_size);
        let b = build_matrix(matrix_size);
        println!(
            "MatrixMul timing: input generation = {:.3} ms",
            input_start.elapsed().as_secs_f64() * 1000.0
        );

        let multiply_start = Instant::now();
        let c = multiply(&a, &b, matrix_size);
        println!(
            "MatrixMul timing: {0}x{0} multiplication = {1:.3} ms",
            matrix_size,
            multiply_start.elapsed().as_secs_f64() * 1000.0
        );

        Self {
            options,
            matrix_size,
            a,
            b,
            c,
            _hasher: PhantomData,
        }
    }
}

// EXAMPLE IMPLEMENTATION
// ================================================================================================

impl<H: ElementHasher> Example for MatrixMulExample<H>
where
    H: ElementHasher<BaseField = BaseElement> + Sync,
{
    fn prove(&self) -> Proof {
        println!(
            "Generating proof for computing {}x{} matrix multiplication",
            self.matrix_size, self.matrix_size
        );

        // create a prover
        let prover = MatrixMulProver::<H>::new(self.options.clone());

        // generate execution trace
        let trace_start = Instant::now();
        let trace =
            info_span!("generate_execution_trace", num_cols = TRACE_WIDTH, steps = field::Empty)
                .in_scope(|| {
                    let trace = prover.build_trace(&self.a, &self.b, &self.c, self.matrix_size);
                    tracing::Span::current().record("steps", trace.length());
                    trace
                });
        println!(
            "MatrixMul timing: trace construction = {:.3} ms ({} steps)",
            trace_start.elapsed().as_secs_f64() * 1000.0,
            trace.length()
        );

        let proof_start = Instant::now();
        let proof = prover.prove(trace).unwrap();
        println!(
            "MatrixMul timing: proof generation (excluding trace) = {:.3} ms",
            proof_start.elapsed().as_secs_f64() * 1000.0
        );
        proof
    }

    fn verify(&self, proof: Proof) -> Result<(), VerifierError> {
        let verify_start = Instant::now();
        let pub_inputs = PublicInputs {
            a: self.a.clone(),
            b: self.b.clone(),
            c: self.c.clone(),
        };
        let acceptable_options =
            winterfell::AcceptableOptions::OptionSet(vec![proof.options().clone()]);
        let result = winterfell::verify::<MatrixMulAir, H, DefaultRandomCoin<H>, MerkleTree<H>>(
            proof,
            pub_inputs,
            &acceptable_options,
        );
        println!(
            "MatrixMul timing: verification = {:.3} ms",
            verify_start.elapsed().as_secs_f64() * 1000.0
        );
        result
    }

    fn verify_with_wrong_inputs(&self, proof: Proof) -> Result<(), VerifierError> {
        let pub_inputs = PublicInputs {
            a: self.a.clone(),
            b: self.b.clone(),
            c: self.c.iter().map(|&e| e + BaseElement::ONE).collect(),
        };
        let acceptable_options =
            winterfell::AcceptableOptions::OptionSet(vec![proof.options().clone()]);
        winterfell::verify::<MatrixMulAir, H, DefaultRandomCoin<H>, MerkleTree<H>>(
            proof,
            pub_inputs,
            &acceptable_options,
        )
    }
}

// HELPER FUNCTIONS
// ================================================================================================

/// Builds an `n x n` matrix (in row-major order) filled with random field elements.
///
/// Random values are used to ensure the trace columns attain the maximum possible polynomial
/// degree (in the same way as the Merkle and Lamport+ examples).
fn build_matrix(n: usize) -> Vec<BaseElement> {
    rand_vector(n * n)
}

/// Computes the product of two `n x n` matrices stored in row-major order.
fn multiply(a: &[BaseElement], b: &[BaseElement], n: usize) -> Vec<BaseElement> {
    let mut c = vec![BaseElement::ZERO; n * n];
    for i in 0..n {
        for j in 0..n {
            let mut sum = BaseElement::ZERO;
            for k in 0..n {
                sum += a[i * n + k] * b[k * n + j];
            }
            c[i * n + j] = sum;
        }
    }
    c
}
