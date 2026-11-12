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
    hash, BaseElement, DefaultRandomCoin, ElementHasher, FieldElement, PhantomData, ProofOptions,
    Prover, PublicInputs, SqlAir, AMOUNT, CATEGORY, CHAIN_IN, COUNT, HASH_ROWS, INVERSE, LEAF,
    MATCH, ROWS_PER_TABLE_ROW, S0, S1, S2, SUM, TOTAL_ROUNDS, TRACE_WIDTH,
};

// SQL AGGREGATE PROVER
// ================================================================================================

pub struct SqlProver<H: ElementHasher> {
    options: ProofOptions,
    target: BaseElement,
    _hasher: PhantomData<H>,
}

impl<H: ElementHasher> SqlProver<H> {
    pub fn new(options: ProofOptions, target: BaseElement) -> Self {
        Self { options, target, _hasher: PhantomData }
    }

    /// Builds one 256-row block per table row: a leaf hash block, a chain hash
    /// block, and the per-row SQL columns carried through both blocks.
    pub fn build_trace(&self, rows: &[(BaseElement, BaseElement)]) -> TraceTable<BaseElement> {
        assert!(rows.len() >= 2, "SQL table must have at least 2 rows");
        assert!(rows.len().is_power_of_two(), "SQL row count must be a power of two");

        let mut trace = TraceTable::new(TRACE_WIDTH, rows.len() * ROWS_PER_TABLE_ROW);
        let round_constants = hash::round_constants();
        let mds = hash::mds();
        let zero = [BaseElement::ZERO; 3];

        let mut chain_prev = BaseElement::ZERO;
        let mut leaf_prev = BaseElement::ZERO;
        let mut count = BaseElement::ZERO;
        let mut sum = BaseElement::ZERO;

        for (index, &(category, amount)) in rows.iter().enumerate() {
            let base = index * ROWS_PER_TABLE_ROW;
            let leaf = hash::hash2(category, amount);
            let chain = hash::hash2(chain_prev, leaf);
            let delta = category - self.target;
            let match_value = if delta == BaseElement::ZERO {
                BaseElement::ONE
            } else {
                BaseElement::ZERO
            };
            let inverse_value = if delta == BaseElement::ZERO {
                BaseElement::ZERO
            } else {
                delta.inv()
            };
            let count_before = count;
            let sum_before = sum;
            count += match_value;
            sum += match_value * amount;

            let mut states = vec![[BaseElement::ZERO; 3]; ROWS_PER_TABLE_ROW];
            let mut state = [BaseElement::ZERO, category, amount];
            states[0] = state;
            for round in 0..TOTAL_ROUNDS {
                state = hash::apply_round(
                    state,
                    &round_constants[round],
                    hash::is_full_round(round),
                    &mds,
                );
                states[round + 1] = state;
            }
            for round in (TOTAL_ROUNDS + 1)..HASH_ROWS {
                state = hash::apply_round(state, &zero, false, &mds);
                states[round] = state;
            }

            let mut state = [BaseElement::ZERO, chain_prev, leaf];
            states[HASH_ROWS] = state;
            for round in 0..TOTAL_ROUNDS {
                state = hash::apply_round(
                    state,
                    &round_constants[round],
                    hash::is_full_round(round),
                    &mds,
                );
                states[HASH_ROWS + round + 1] = state;
            }
            for round in (TOTAL_ROUNDS + 1)..HASH_ROWS {
                state = hash::apply_round(state, &zero, false, &mds);
                states[HASH_ROWS + round] = state;
            }

            for row in 0..ROWS_PER_TABLE_ROW {
                trace.set(S0, base + row, states[row][0]);
                trace.set(S1, base + row, states[row][1]);
                trace.set(S2, base + row, states[row][2]);
                trace.set(CATEGORY, base + row, category);
                trace.set(AMOUNT, base + row, amount);
                trace.set(LEAF, base + row, if row <= TOTAL_ROUNDS { leaf_prev } else { leaf });
                trace.set(
                    CHAIN_IN,
                    base + row,
                    if row <= HASH_ROWS + TOTAL_ROUNDS {
                        chain_prev
                    } else {
                        chain
                    },
                );
                trace.set(MATCH, base + row, match_value);
                trace.set(INVERSE, base + row, inverse_value);
                trace.set(
                    COUNT,
                    base + row,
                    if row <= HASH_ROWS + TOTAL_ROUNDS {
                        count_before
                    } else {
                        count
                    },
                );
                trace.set(
                    SUM,
                    base + row,
                    if row <= HASH_ROWS + TOTAL_ROUNDS {
                        sum_before
                    } else {
                        sum
                    },
                );
            }

            chain_prev = chain;
            leaf_prev = leaf;
        }

        trace
    }
}

impl<H: ElementHasher> Prover for SqlProver<H>
where
    H: ElementHasher<BaseField = BaseElement> + Sync,
{
    type BaseField = BaseElement;
    type Air = SqlAir;
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
        let last_step = trace.length() - 1;
        PublicInputs {
            target: self.target,
            count: trace.get(COUNT, last_step),
            sum: trace.get(SUM, last_step),
            digest: trace.get(CHAIN_IN, last_step),
        }
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
