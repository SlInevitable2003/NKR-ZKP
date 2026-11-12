// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use winterfell::{
    crypto::MerkleTree, matrix::ColMatrix, AuxRandElements, CompositionPoly, CompositionPolyTrace,
    ConstraintCompositionCoefficients, DefaultConstraintCommitment, DefaultConstraintEvaluator,
    DefaultTraceLde, PartitionOptions, StarkDomain, Trace, TraceInfo, TracePolyTable, TraceTable,
};

use super::{
    hash, BaseElement, DefaultRandomCoin, ElementHasher, FieldElement, PhantomData, PoseidonAir,
    ProofOptions, Prover, PublicInputs, ROUNDS_PER_HASH, TOTAL_ROUNDS, TRACE_WIDTH, WIDTH,
};

// POSEIDON PROVER
// ================================================================================================

pub struct PoseidonProver<H: ElementHasher> {
    options: ProofOptions,
    _hasher: PhantomData<H>,
}

impl<H: ElementHasher> PoseidonProver<H> {
    pub fn new(options: ProofOptions) -> Self {
        Self { options, _hasher: PhantomData }
    }

    /// Builds one trace block per hash: 65 round states followed by the final
    /// state, padded with extra partial rounds to ROUNDS_PER_HASH rows.
    pub fn build_trace(&self, inputs: &[BaseElement]) -> TraceTable<BaseElement> {
        assert!(inputs.len() % 2 == 0, "Poseidon inputs must come in pairs");
        let count = inputs.len() / 2;
        let mut trace = TraceTable::new(TRACE_WIDTH, count * ROUNDS_PER_HASH);
        let round_constants = hash::round_constants();
        let mds = hash::mds();

        for index in 0..count {
            let start = index * ROUNDS_PER_HASH;
            let mut state = [BaseElement::ZERO, inputs[2 * index], inputs[2 * index + 1]];
            for round in 0..TOTAL_ROUNDS {
                for column in 0..WIDTH {
                    trace.set(column, start + round, state[column]);
                }
                state = hash::apply_round(
                    state,
                    &round_constants[round],
                    hash::is_full_round(round),
                    &mds,
                );
            }
            for column in 0..WIDTH {
                trace.set(column, start + TOTAL_ROUNDS, state[column]);
            }
            for row in (TOTAL_ROUNDS + 1)..ROUNDS_PER_HASH {
                state = hash::apply_round(state, &[BaseElement::ZERO; WIDTH], false, &mds);
                for column in 0..WIDTH {
                    trace.set(column, start + row, state[column]);
                }
            }
        }
        trace
    }
}

impl<H: ElementHasher> Prover for PoseidonProver<H>
where
    H: ElementHasher<BaseField = BaseElement> + Sync,
{
    type BaseField = BaseElement;
    type Air = PoseidonAir;
    type Trace = TraceTable<BaseElement>;
    type HashFn = H;
    type VC = MerkleTree<H>;
    type RandomCoin = DefaultRandomCoin<Self::HashFn>;
    type TraceLde<E: FieldElement<BaseField = Self::BaseField>> =
        DefaultTraceLde<E, Self::HashFn, Self::VC>;
    type ConstraintCommitment<E: FieldElement<BaseField = Self::BaseField>> =
        DefaultConstraintCommitment<E, H, Self::VC>;
    type ConstraintEvaluator<'a, E: FieldElement<BaseField = Self::BaseField>> =
        DefaultConstraintEvaluator<'a, Self::Air, E>;

    fn get_pub_inputs(&self, trace: &Self::Trace) -> PublicInputs {
        let count = trace.length() / ROUNDS_PER_HASH;
        let mut inputs = Vec::with_capacity(2 * count);
        let mut digests = Vec::with_capacity(count);
        for index in 0..count {
            let start = index * ROUNDS_PER_HASH;
            inputs.push(trace.get(1, start));
            inputs.push(trace.get(2, start));
            digests.push(trace.get(0, start + TOTAL_ROUNDS));
        }
        PublicInputs { inputs, digests }
    }

    fn options(&self) -> &ProofOptions {
        &self.options
    }

    fn new_trace_lde<E: FieldElement<BaseField = Self::BaseField>>(
        &self,
        trace_info: &TraceInfo,
        main_trace: &ColMatrix<Self::BaseField>,
        domain: &StarkDomain<Self::BaseField>,
        partition_option: PartitionOptions,
    ) -> (Self::TraceLde<E>, TracePolyTable<E>) {
        DefaultTraceLde::new(trace_info, main_trace, domain, partition_option)
    }

    fn new_evaluator<'a, E: FieldElement<BaseField = Self::BaseField>>(
        &self,
        air: &'a Self::Air,
        aux_rand_elements: Option<AuxRandElements<E>>,
        composition_coefficients: ConstraintCompositionCoefficients<E>,
    ) -> Self::ConstraintEvaluator<'a, E> {
        DefaultConstraintEvaluator::new(air, aux_rand_elements, composition_coefficients)
    }

    fn build_constraint_commitment<E: FieldElement<BaseField = Self::BaseField>>(
        &self,
        composition_poly_trace: CompositionPolyTrace<E>,
        num_constraint_composition_columns: usize,
        domain: &StarkDomain<Self::BaseField>,
        partition_options: PartitionOptions,
    ) -> (Self::ConstraintCommitment<E>, CompositionPoly<E>) {
        DefaultConstraintCommitment::new(
            composition_poly_trace,
            num_constraint_composition_columns,
            domain,
            partition_options,
        )
    }
}
