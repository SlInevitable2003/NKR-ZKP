// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use core::marker::PhantomData;
use std::time::Instant;

use winterfell::{
    crypto::{DefaultRandomCoin, ElementHasher, MerkleTree},
    math::{fields::f128::BaseElement, FieldElement, StarkField},
    Proof, ProofOptions, Prover, Trace, VerifierError,
};

use crate::poseidon::hash;
use crate::{Blake3_192, Blake3_256, Example, ExampleOptions, HashFunction, Sha3_256};

mod air;
use air::{PublicInputs, SqlAir};

mod prover;
use prover::SqlProver;

#[cfg(test)]
mod tests;

// CONSTANTS
// ================================================================================================

pub const WIDTH: usize = crate::poseidon::WIDTH;
pub const HASH_ROWS: usize = crate::poseidon::ROUNDS_PER_HASH;
pub const ROWS_PER_TABLE_ROW: usize = 2 * HASH_ROWS;
pub const TOTAL_ROUNDS: usize = hash::TOTAL_ROUNDS;

pub const TRACE_WIDTH: usize = 11;
pub const S0: usize = 0;
pub const S1: usize = 1;
pub const S2: usize = 2;
pub const CATEGORY: usize = 3;
pub const AMOUNT: usize = 4;
pub const LEAF: usize = 5;
pub const CHAIN_IN: usize = 6;
pub const MATCH: usize = 7;
pub const INVERSE: usize = 8;
pub const COUNT: usize = 9;
pub const SUM: usize = 10;

// SQL AGGREGATE EXAMPLE
//
// The table is committed by a Poseidon hash chain over the ordered rows:
//   chain_0 = 0, leaf_i = H(category_i, amount_i),
//   chain_i = H(chain_{i-1}, leaf_i), digest = chain_n.
// Each table row takes two 128-row Poseidon blocks (leaf then chain) plus the
// COUNT/SUM accumulation. Unlike the Groth16/PLONK circuits this example does
// not decompose categories/amounts into UINT32 bits and uses a hash chain
// instead of a Merkle root.

pub struct QueryResult {
    pub count: BaseElement,
    pub sum: BaseElement,
    pub digest: BaseElement,
}

pub fn evaluate(rows: &[(BaseElement, BaseElement)], target: BaseElement) -> QueryResult {
    let mut count = BaseElement::ZERO;
    let mut sum = BaseElement::ZERO;
    let mut digest = BaseElement::ZERO;
    for &(category, amount) in rows {
        let leaf = hash::hash2(category, amount);
        digest = hash::hash2(digest, leaf);
        if category == target {
            count += BaseElement::ONE;
            sum += amount;
        }
    }
    QueryResult { count, sum, digest }
}

pub fn get_example(options: &ExampleOptions, num_rows: usize) -> Result<Box<dyn Example>, String> {
    if num_rows < 2 || !num_rows.is_power_of_two() {
        return Err("SQL row count must be a power of two and at least 2".to_string());
    }

    let profile = options.profile_enabled();
    let density = options.density();
    let amounts = density.generate(num_rows);
    let mut rows = Vec::with_capacity(num_rows);
    for amount in amounts {
        let category = BaseElement::new(rand_utils::rand_value::<BaseElement>().as_int() % 16);
        rows.push((category, amount));
    }
    let target = rows[0].0;
    let zeros = rows.iter().filter(|(_, amount)| *amount == BaseElement::ZERO).count();
    println!("Density: {density} (amounts: {}/{} nonzero)", num_rows - zeros, num_rows);

    let (options, hash_fn) = options.to_proof_options(28, 8);

    match hash_fn {
        HashFunction::Blake3_192 => {
            Ok(Box::new(SqlExample::<Blake3_192>::new(rows, target, options, profile)))
        },
        HashFunction::Blake3_256 => {
            Ok(Box::new(SqlExample::<Blake3_256>::new(rows, target, options, profile)))
        },
        HashFunction::Sha3_256 => {
            Ok(Box::new(SqlExample::<Sha3_256>::new(rows, target, options, profile)))
        },
        _ => Err("The specified hash function cannot be used with this example.".to_string()),
    }
}

pub struct SqlExample<H: ElementHasher> {
    options: ProofOptions,
    rows: Vec<(BaseElement, BaseElement)>,
    target: BaseElement,
    result: QueryResult,
    profile: bool,
    _hasher: PhantomData<H>,
}

impl<H: ElementHasher> SqlExample<H> {
    pub fn new(
        rows: Vec<(BaseElement, BaseElement)>,
        target: BaseElement,
        options: ProofOptions,
        profile: bool,
    ) -> Self {
        assert!(rows.len() >= 2, "SQL table must have at least 2 rows");
        assert!(rows.len().is_power_of_two(), "SQL row count must be a power of two");
        let result = evaluate(&rows, target);
        Self {
            options,
            rows,
            target,
            result,
            profile,
            _hasher: PhantomData,
        }
    }
}

// EXAMPLE IMPLEMENTATION
// ================================================================================================

impl<H: ElementHasher> Example for SqlExample<H>
where
    H: ElementHasher<BaseField = BaseElement> + Sync,
{
    fn prove(&self) -> Proof {
        println!("Generating proof for COUNT/SUM over a {}-row SQL table", self.rows.len());

        let prover = SqlProver::<H>::new(self.options.clone(), self.target);

        let trace_start = Instant::now();
        let trace = prover.build_trace(&self.rows);
        if self.profile {
            println!(
                "[profile] stark.sql.trace_construction = {:.3} ms ({} steps)",
                trace_start.elapsed().as_secs_f64() * 1000.0,
                trace.length()
            );
        }

        let proof_start = Instant::now();
        let proof = prover.prove(trace).unwrap();
        if self.profile {
            println!(
                "[profile] stark.sql.proof_generation_excluding_trace = {:.3} ms",
                proof_start.elapsed().as_secs_f64() * 1000.0
            );
        }
        proof
    }

    fn verify(&self, proof: Proof) -> Result<(), VerifierError> {
        let verify_start = Instant::now();
        let pub_inputs = PublicInputs {
            target: self.target,
            count: self.result.count,
            sum: self.result.sum,
            digest: self.result.digest,
        };
        let acceptable_options =
            winterfell::AcceptableOptions::OptionSet(vec![proof.options().clone()]);
        let result = winterfell::verify::<SqlAir, H, DefaultRandomCoin<H>, MerkleTree<H>>(
            proof,
            pub_inputs,
            &acceptable_options,
        );
        if self.profile {
            println!(
                "[profile] stark.sql.verification = {:.3} ms",
                verify_start.elapsed().as_secs_f64() * 1000.0
            );
        }
        result
    }

    fn verify_with_wrong_inputs(&self, proof: Proof) -> Result<(), VerifierError> {
        let pub_inputs = PublicInputs {
            target: self.target,
            count: self.result.count,
            sum: self.result.sum + BaseElement::ONE,
            digest: self.result.digest,
        };
        let acceptable_options =
            winterfell::AcceptableOptions::OptionSet(vec![proof.options().clone()]);
        winterfell::verify::<SqlAir, H, DefaultRandomCoin<H>, MerkleTree<H>>(
            proof,
            pub_inputs,
            &acceptable_options,
        )
    }
}
