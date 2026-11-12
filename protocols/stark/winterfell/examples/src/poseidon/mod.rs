// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use core::marker::PhantomData;
use std::time::Instant;

use winterfell::{
    crypto::{DefaultRandomCoin, ElementHasher, MerkleTree},
    math::{fields::f128::BaseElement, FieldElement},
    Proof, ProofOptions, Prover, Trace, VerifierError,
};

use crate::{Blake3_192, Blake3_256, Example, ExampleOptions, HashFunction, Sha3_256};

pub mod hash;

mod air;
use air::{PoseidonAir, PublicInputs};

mod prover;
use prover::PoseidonProver;

#[cfg(test)]
mod tests;

// CONSTANTS
// ================================================================================================

pub const WIDTH: usize = hash::WIDTH;
pub const TRACE_WIDTH: usize = WIDTH;
pub const TOTAL_ROUNDS: usize = hash::TOTAL_ROUNDS;
pub const ROUNDS_PER_HASH: usize = hash::ROUNDS_PER_HASH;

// POSEIDON EXAMPLE
// ================================================================================================

pub fn get_example(
    options: &ExampleOptions,
    num_hashes: usize,
) -> Result<Box<dyn Example>, String> {
    if !num_hashes.is_power_of_two() {
        return Err("Poseidon hash count must be a power of two".to_string());
    }

    let profile = options.profile_enabled();
    let density = options.density();
    let inputs = density.generate(2 * num_hashes);
    let digests: Vec<BaseElement> = (0..num_hashes)
        .map(|index| hash::hash2(inputs[2 * index], inputs[2 * index + 1]))
        .collect();
    let zeros = inputs.iter().filter(|&&value| value == BaseElement::ZERO).count();
    println!("Density: {density} ({} inputs, {} zeros)", inputs.len(), zeros);

    let (options, hash_fn) = options.to_proof_options(28, 8);

    match hash_fn {
        HashFunction::Blake3_192 => Ok(Box::new(PoseidonExample::<Blake3_192>::new(
            num_hashes, options, profile, inputs, digests,
        ))),
        HashFunction::Blake3_256 => Ok(Box::new(PoseidonExample::<Blake3_256>::new(
            num_hashes, options, profile, inputs, digests,
        ))),
        HashFunction::Sha3_256 => Ok(Box::new(PoseidonExample::<Sha3_256>::new(
            num_hashes, options, profile, inputs, digests,
        ))),
        _ => Err("The specified hash function cannot be used with this example.".to_string()),
    }
}

pub struct PoseidonExample<H: ElementHasher> {
    options: ProofOptions,
    num_hashes: usize,
    inputs: Vec<BaseElement>,
    digests: Vec<BaseElement>,
    profile: bool,
    _hasher: PhantomData<H>,
}

impl<H: ElementHasher> PoseidonExample<H> {
    pub fn new(
        num_hashes: usize,
        options: ProofOptions,
        profile: bool,
        inputs: Vec<BaseElement>,
        digests: Vec<BaseElement>,
    ) -> Self {
        assert!(num_hashes.is_power_of_two(), "Poseidon hash count must be a power of two");
        assert_eq!(inputs.len(), 2 * num_hashes);
        assert_eq!(digests.len(), num_hashes);
        Self {
            options,
            num_hashes,
            inputs,
            digests,
            profile,
            _hasher: PhantomData,
        }
    }
}

// EXAMPLE IMPLEMENTATION
// ================================================================================================

impl<H: ElementHasher> Example for PoseidonExample<H>
where
    H: ElementHasher<BaseField = BaseElement> + Sync,
{
    fn prove(&self) -> Proof {
        println!(
            "Generating proof for {} Poseidon hashes ({}x{} inputs)",
            self.num_hashes, self.num_hashes, 2
        );

        let prover = PoseidonProver::<H>::new(self.options.clone());

        let trace_start = Instant::now();
        let trace = prover.build_trace(&self.inputs);
        if self.profile {
            println!(
                "[profile] stark.poseidon.trace_construction = {:.3} ms ({} steps)",
                trace_start.elapsed().as_secs_f64() * 1000.0,
                trace.length()
            );
        }

        let proof_start = Instant::now();
        let proof = prover.prove(trace).unwrap();
        if self.profile {
            println!(
                "[profile] stark.poseidon.proof_generation_excluding_trace = {:.3} ms",
                proof_start.elapsed().as_secs_f64() * 1000.0
            );
        }
        proof
    }

    fn verify(&self, proof: Proof) -> Result<(), VerifierError> {
        let verify_start = Instant::now();
        let pub_inputs = PublicInputs {
            inputs: self.inputs.clone(),
            digests: self.digests.clone(),
        };
        let acceptable_options =
            winterfell::AcceptableOptions::OptionSet(vec![proof.options().clone()]);
        let result = winterfell::verify::<PoseidonAir, H, DefaultRandomCoin<H>, MerkleTree<H>>(
            proof,
            pub_inputs,
            &acceptable_options,
        );
        if self.profile {
            println!(
                "[profile] stark.poseidon.verification = {:.3} ms",
                verify_start.elapsed().as_secs_f64() * 1000.0
            );
        }
        result
    }

    fn verify_with_wrong_inputs(&self, proof: Proof) -> Result<(), VerifierError> {
        let mut digests = self.digests.clone();
        digests[0] += BaseElement::ONE;
        let pub_inputs = PublicInputs { inputs: self.inputs.clone(), digests };
        let acceptable_options =
            winterfell::AcceptableOptions::OptionSet(vec![proof.options().clone()]);
        winterfell::verify::<PoseidonAir, H, DefaultRandomCoin<H>, MerkleTree<H>>(
            proof,
            pub_inputs,
            &acceptable_options,
        )
    }
}
