// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use winterfell::{
    math::ToElements, Air, AirContext, Assertion, EvaluationFrame, TraceInfo,
    TransitionConstraintDegree,
};

use super::{
    hash, BaseElement, FieldElement, ProofOptions, AMOUNT, CATEGORY, CHAIN_IN, COUNT, HASH_ROWS,
    INVERSE, LEAF, MATCH, ROWS_PER_TABLE_ROW, S0, S1, S2, SUM, TOTAL_ROUNDS, TRACE_WIDTH, WIDTH,
};

// CONSTANTS
// ================================================================================================

const FULL_FLAG: usize = 0;
const RC0: usize = 1;
const RC1: usize = 2;
const RC2: usize = 3;
const BOUNDARY_FLAG: usize = 4;
const LEAF_START: usize = 5;
const CHAIN_START: usize = 6;
const LEAF_DIGEST: usize = 7;
const CHAIN_DIGEST: usize = 8;
const ROW_END: usize = 9;
const NUM_PERIODIC_COLUMNS: usize = 10;

const NUM_ASSERTIONS: usize = 6;

// SQL AIR
// ================================================================================================

pub struct PublicInputs {
    pub target: BaseElement,
    pub count: BaseElement,
    pub sum: BaseElement,
    pub digest: BaseElement,
}

impl ToElements<BaseElement> for PublicInputs {
    fn to_elements(&self) -> Vec<BaseElement> {
        vec![self.target, self.count, self.sum, self.digest]
    }
}

pub struct SqlAir {
    context: AirContext<BaseElement>,
    target: BaseElement,
    expected_count: BaseElement,
    expected_sum: BaseElement,
    expected_digest: BaseElement,
    mds: [[BaseElement; WIDTH]; WIDTH],
}

impl Air for SqlAir {
    type BaseField = BaseElement;
    type PublicInputs = PublicInputs;

    // CONSTRUCTOR
    // --------------------------------------------------------------------------------------------
    fn new(trace_info: TraceInfo, pub_inputs: PublicInputs, options: ProofOptions) -> Self {
        assert_eq!(TRACE_WIDTH, trace_info.width());
        assert_eq!(0, trace_info.length() % ROWS_PER_TABLE_ROW);

        let mut degrees = Vec::with_capacity(TRACE_WIDTH + 6);
        // Poseidon round transition: x^5 with the full/boundary schedule columns.
        for _ in 0..3 {
            degrees.push(TransitionConstraintDegree::with_cycles(5, vec![HASH_ROWS, HASH_ROWS]));
        }
        // Block-start loads, carry constraints, leaf/chain/count updates.
        for _ in 0..6 {
            degrees.push(TransitionConstraintDegree::with_cycles(1, vec![ROWS_PER_TABLE_ROW]));
        }
        for _ in 0..2 {
            degrees.push(TransitionConstraintDegree::with_cycles(1, vec![ROWS_PER_TABLE_ROW]));
        }
        // Equality gadget (degree 2).
        for _ in 0..2 {
            degrees.push(TransitionConstraintDegree::new(2));
        }
        for _ in 0..3 {
            degrees.push(TransitionConstraintDegree::with_cycles(1, vec![ROWS_PER_TABLE_ROW]));
        }
        // SUM update: match * amount.
        degrees.push(TransitionConstraintDegree::with_cycles(2, vec![ROWS_PER_TABLE_ROW]));

        SqlAir {
            context: AirContext::new(trace_info, degrees, NUM_ASSERTIONS, options),
            target: pub_inputs.target,
            expected_count: pub_inputs.count,
            expected_sum: pub_inputs.sum,
            expected_digest: pub_inputs.digest,
            mds: hash::mds(),
        }
    }

    fn context(&self) -> &AirContext<Self::BaseField> {
        &self.context
    }

    fn evaluate_transition<E: FieldElement<BaseField = Self::BaseField>>(
        &self,
        frame: &EvaluationFrame<E>,
        periodic_values: &[E],
        result: &mut [E],
    ) {
        debug_assert_eq!(NUM_PERIODIC_COLUMNS, periodic_values.len());
        let current = frame.current();
        let next = frame.next();

        // 1 ----- Poseidon round transition ------------------------------------------------
        let full = periodic_values[FULL_FLAG];
        let round_constants = [periodic_values[RC0], periodic_values[RC1], periodic_values[RC2]];
        let boundary = periodic_values[BOUNDARY_FLAG];
        let partial = E::ONE - full;
        let sbox = |value: E| {
            let squared = value * value;
            squared * squared * value
        };

        let mut added = [E::ZERO; WIDTH];
        for i in 0..WIDTH {
            added[i] = current[i] + round_constants[i];
        }
        let sboxed = [
            sbox(added[0]),
            full * sbox(added[1]) + partial * added[1],
            full * sbox(added[2]) + partial * added[2],
        ];
        let mut mixed = [E::ZERO; WIDTH];
        for row in 0..WIDTH {
            for column in 0..WIDTH {
                mixed[row] += E::from(self.mds[row][column]) * sboxed[column];
            }
        }
        let not_boundary = E::ONE - boundary;
        for i in 0..WIDTH {
            result[i] = not_boundary * (next[i] - mixed[i]);
        }

        // 2 ----- block-start loads ---------------------------------------------------------
        let leaf_start = periodic_values[LEAF_START];
        let chain_start = periodic_values[CHAIN_START];
        result[3] = leaf_start * current[S0];
        result[4] = leaf_start * (current[S1] - current[CATEGORY]);
        result[5] = leaf_start * (current[S2] - current[AMOUNT]);
        result[6] = chain_start * current[S0];
        result[7] = chain_start * (current[S1] - current[CHAIN_IN]);
        result[8] = chain_start * (current[S2] - current[LEAF]);

        // 3 ----- row-column carry ----------------------------------------------------------
        let row_end = periodic_values[ROW_END];
        let carry = E::ONE - row_end;
        result[9] = carry * (next[CATEGORY] - current[CATEGORY]);
        result[10] = carry * (next[AMOUNT] - current[AMOUNT]);

        // 4 ----- category == target gadget -------------------------------------------------
        let delta = current[CATEGORY] - E::from(self.target);
        result[11] = delta * current[INVERSE] - (E::ONE - current[MATCH]);
        result[12] = delta * current[MATCH];

        // 5 ----- leaf / chain / cumulative updates -----------------------------------------
        let leaf_digest = periodic_values[LEAF_DIGEST];
        let chain_digest = periodic_values[CHAIN_DIGEST];
        result[13] = next[LEAF] - (current[LEAF] + leaf_digest * (current[S0] - current[LEAF]));
        result[14] =
            next[CHAIN_IN] - (current[CHAIN_IN] + chain_digest * (current[S0] - current[CHAIN_IN]));
        result[15] = next[COUNT] - (current[COUNT] + chain_digest * current[MATCH]);
        result[16] = next[SUM] - (current[SUM] + chain_digest * current[MATCH] * current[AMOUNT]);
    }

    fn get_assertions(&self) -> Vec<Assertion<Self::BaseField>> {
        let last_step = self.trace_length() - 1;
        vec![
            Assertion::single(CHAIN_IN, 0, BaseElement::ZERO),
            Assertion::single(COUNT, 0, BaseElement::ZERO),
            Assertion::single(SUM, 0, BaseElement::ZERO),
            Assertion::single(CHAIN_IN, last_step, self.expected_digest),
            Assertion::single(COUNT, last_step, self.expected_count),
            Assertion::single(SUM, last_step, self.expected_sum),
        ]
    }

    fn get_periodic_column_values(&self) -> Vec<Vec<Self::BaseField>> {
        let round_constants = hash::round_constants();

        let mut full_flag = vec![BaseElement::ZERO; HASH_ROWS];
        let mut rc0 = vec![BaseElement::ZERO; HASH_ROWS];
        let mut rc1 = vec![BaseElement::ZERO; HASH_ROWS];
        let mut rc2 = vec![BaseElement::ZERO; HASH_ROWS];
        let mut boundary_flag = vec![BaseElement::ZERO; HASH_ROWS];
        for round in 0..TOTAL_ROUNDS {
            if hash::is_full_round(round) {
                full_flag[round] = BaseElement::ONE;
            }
            rc0[round] = round_constants[round][0];
            rc1[round] = round_constants[round][1];
            rc2[round] = round_constants[round][2];
        }
        boundary_flag[HASH_ROWS - 1] = BaseElement::ONE;

        let mut leaf_start = vec![BaseElement::ZERO; ROWS_PER_TABLE_ROW];
        let mut chain_start = vec![BaseElement::ZERO; ROWS_PER_TABLE_ROW];
        let mut leaf_digest = vec![BaseElement::ZERO; ROWS_PER_TABLE_ROW];
        let mut chain_digest = vec![BaseElement::ZERO; ROWS_PER_TABLE_ROW];
        let mut row_end = vec![BaseElement::ZERO; ROWS_PER_TABLE_ROW];
        leaf_start[0] = BaseElement::ONE;
        chain_start[HASH_ROWS] = BaseElement::ONE;
        leaf_digest[TOTAL_ROUNDS] = BaseElement::ONE;
        chain_digest[HASH_ROWS + TOTAL_ROUNDS] = BaseElement::ONE;
        row_end[ROWS_PER_TABLE_ROW - 1] = BaseElement::ONE;

        vec![
            full_flag,
            rc0,
            rc1,
            rc2,
            boundary_flag,
            leaf_start,
            chain_start,
            leaf_digest,
            chain_digest,
            row_end,
        ]
    }
}
