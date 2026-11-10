// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use core::marker::PhantomData;
use std::time::Instant;

use tracing::{field, info_span};
use winterfell::{
    crypto::{DefaultRandomCoin, ElementHasher, MerkleTree},
    math::{fields::f128::BaseElement, FieldElement},
    Proof, ProofOptions, Prover, Trace, VerifierError,
};

use crate::{Blake3_192, Blake3_256, Example, ExampleOptions, HashFunction, Sha3_256};

mod air;
use air::{FibAir, PublicInputs};

mod prover;
use prover::FibProver;

#[cfg(test)]
mod tests;

// CONSTANTS
// ================================================================================================

const TRACE_WIDTH: usize = 2;

// FIBONACCI EXAMPLE
// ================================================================================================

pub fn get_example(
    options: &ExampleOptions,
    sequence_length: usize,
) -> Result<Box<dyn Example>, String> {
    let profile = options.profile_enabled();
    let mode = options.data_mode();
    let input_start = Instant::now();
    #[cfg(feature = "std")]
    let seeds = {
        let values = mode.generate(2);
        [values[0], values[1]]
    };
    #[cfg(not(feature = "std"))]
    let seeds = {
        if mode != crate::input_data::DataMode::Dense {
            return Err("random input data modes require the std feature".to_string());
        }
        [BaseElement::ONE; 2]
    };
    println!(
        "Data mode: {mode} (2 inputs, {} zeros)",
        seeds.iter().filter(|&&value| value == BaseElement::ZERO).count()
    );
    if profile {
        println!(
            "[profile] stark.fibonacci.input_generation = {:.3} ms",
            input_start.elapsed().as_secs_f64() * 1000.0
        );
    }
    let (options, hash_fn) = options.to_proof_options(28, 8);

    match hash_fn {
        HashFunction::Blake3_192 => Ok(Box::new(FibExample::<Blake3_192>::new_with_inputs(
            sequence_length,
            options,
            seeds,
            profile,
        ))),
        HashFunction::Blake3_256 => Ok(Box::new(FibExample::<Blake3_256>::new_with_inputs(
            sequence_length,
            options,
            seeds,
            profile,
        ))),
        HashFunction::Sha3_256 => Ok(Box::new(FibExample::<Sha3_256>::new_with_inputs(
            sequence_length,
            options,
            seeds,
            profile,
        ))),
        _ => Err("The specified hash function cannot be used with this example.".to_string()),
    }
}

pub struct FibExample<H: ElementHasher> {
    options: ProofOptions,
    sequence_length: usize,
    seeds: [BaseElement; 2],
    result: BaseElement,
    profile: bool,
    _hasher: PhantomData<H>,
}

impl<H: ElementHasher> FibExample<H> {
    pub fn new(sequence_length: usize, options: ProofOptions) -> Self {
        Self::new_with_inputs(sequence_length, options, [BaseElement::ONE; 2], false)
    }

    pub fn new_with_inputs(
        sequence_length: usize,
        options: ProofOptions,
        seeds: [BaseElement; 2],
        profile: bool,
    ) -> Self {
        assert!(sequence_length.is_power_of_two(), "sequence length must be a power of 2");
        assert!(sequence_length >= 16, "sequence length must be at least 16");

        // compute Fibonacci sequence
        let now = Instant::now();
        let mut previous = seeds[0];
        let mut result = seeds[1];
        for _ in 0..sequence_length - 2 {
            let next = previous + result;
            previous = result;
            result = next;
        }
        println!(
            "Computed Fibonacci sequence up to {}th term in {} ms",
            sequence_length,
            now.elapsed().as_millis()
        );
        if profile {
            println!(
                "[profile] stark.fibonacci.sequence_computation = {:.3} ms",
                now.elapsed().as_secs_f64() * 1000.0
            );
        }

        FibExample {
            options,
            sequence_length,
            seeds,
            result,
            profile,
            _hasher: PhantomData,
        }
    }
}

// EXAMPLE IMPLEMENTATION
// ================================================================================================

impl<H: ElementHasher> Example for FibExample<H>
where
    H: ElementHasher<BaseField = BaseElement> + Sync,
{
    fn prove(&self) -> Proof {
        println!(
            "Generating proof for computing Fibonacci sequence (2 terms per step) up to {}th term",
            self.sequence_length
        );

        // create a prover
        let prover = FibProver::<H>::new(self.options.clone());

        // generate execution trace
        let trace_start = Instant::now();
        let trace =
            info_span!("generate_execution_trace", num_cols = TRACE_WIDTH, steps = field::Empty)
                .in_scope(|| {
                    let trace = prover.build_trace(self.sequence_length, self.seeds);
                    tracing::Span::current().record("steps", trace.length());
                    trace
                });

        if self.profile {
            println!(
                "[profile] stark.fibonacci.trace_construction = {:.3} ms ({} steps)",
                trace_start.elapsed().as_secs_f64() * 1000.0,
                trace.length()
            );
        }
        let proof_start = Instant::now();
        let proof = prover.prove(trace).unwrap();
        if self.profile {
            println!(
                "[profile] stark.fibonacci.proof_generation_excluding_trace = {:.3} ms",
                proof_start.elapsed().as_secs_f64() * 1000.0
            );
        }
        proof
    }

    fn verify(&self, proof: Proof) -> Result<(), VerifierError> {
        let verify_start = Instant::now();
        let acceptable_options =
            winterfell::AcceptableOptions::OptionSet(vec![proof.options().clone()]);

        let result = winterfell::verify::<FibAir, H, DefaultRandomCoin<H>, MerkleTree<H>>(
            proof,
            PublicInputs { seeds: self.seeds, result: self.result },
            &acceptable_options,
        );
        if self.profile {
            println!(
                "[profile] stark.fibonacci.verification = {:.3} ms",
                verify_start.elapsed().as_secs_f64() * 1000.0
            );
        }
        result
    }

    fn verify_with_wrong_inputs(&self, proof: Proof) -> Result<(), VerifierError> {
        let acceptable_options =
            winterfell::AcceptableOptions::OptionSet(vec![proof.options().clone()]);
        winterfell::verify::<FibAir, H, DefaultRandomCoin<H>, MerkleTree<H>>(
            proof,
            PublicInputs {
                seeds: self.seeds,
                result: self.result + BaseElement::ONE,
            },
            &acceptable_options,
        )
    }
}
