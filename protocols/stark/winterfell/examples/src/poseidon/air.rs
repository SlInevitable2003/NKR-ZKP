// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use winterfell::{
    math::ToElements, Air, AirContext, Assertion, EvaluationFrame, TraceInfo,
    TransitionConstraintDegree,
};

use super::{
    hash, BaseElement, FieldElement, ProofOptions, ROUNDS_PER_HASH, TOTAL_ROUNDS, TRACE_WIDTH,
    WIDTH,
};

// CONSTANTS
// ================================================================================================

/// Periodic columns per hash block: full-round flag, three round constants, and
/// a boundary flag set on the last row of the block.
const NUM_PERIODIC_COLUMNS: usize = 5;
const FULL_FLAG: usize = 0;
const RC0: usize = 1;
const RC1: usize = 2;
const RC2: usize = 3;
const BOUNDARY_FLAG: usize = 4;

// POSEIDON AIR
// ================================================================================================

pub struct PublicInputs {
    pub inputs: Vec<BaseElement>,
    pub digests: Vec<BaseElement>,
}

impl ToElements<BaseElement> for PublicInputs {
    fn to_elements(&self) -> Vec<BaseElement> {
        let mut result = self.inputs.clone();
        result.extend_from_slice(&self.digests);
        result
    }
}

pub struct PoseidonAir {
    context: AirContext<BaseElement>,
    inputs: Vec<BaseElement>,
    digests: Vec<BaseElement>,
    mds: [[BaseElement; WIDTH]; WIDTH],
}

impl Air for PoseidonAir {
    type BaseField = BaseElement;
    type PublicInputs = PublicInputs;

    // CONSTRUCTOR
    // --------------------------------------------------------------------------------------------
    fn new(trace_info: TraceInfo, pub_inputs: PublicInputs, options: ProofOptions) -> Self {
        assert_eq!(TRACE_WIDTH, trace_info.width());
        assert_eq!(pub_inputs.inputs.len(), 2 * pub_inputs.digests.len());
        assert_eq!(trace_info.length(), ROUNDS_PER_HASH * pub_inputs.digests.len());

        // Every round constraint has base degree 5 (the x^5 S-box) and uses the
        // periodic schedule columns, which all share the same cycle length.
        let degrees =
            vec![
                TransitionConstraintDegree::with_cycles(5, vec![ROUNDS_PER_HASH, ROUNDS_PER_HASH],);
                TRACE_WIDTH
            ];
        let num_assertions = 4 * pub_inputs.digests.len();
        PoseidonAir {
            context: AirContext::new(trace_info, degrees, num_assertions, options),
            inputs: pub_inputs.inputs,
            digests: pub_inputs.digests,
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

        // The transition into the first row of the next hash block is asserted
        // directly, so it is not required to follow the round function.
        let not_boundary = E::ONE - boundary;
        for i in 0..WIDTH {
            result[i] = not_boundary * (next[i] - mixed[i]);
        }
    }

    fn get_assertions(&self) -> Vec<Assertion<Self::BaseField>> {
        let mut assertions = Vec::with_capacity(4 * self.digests.len());
        for (index, digest) in self.digests.iter().enumerate() {
            let start = index * ROUNDS_PER_HASH;
            assertions.push(Assertion::single(0, start, BaseElement::ZERO));
            assertions.push(Assertion::single(1, start, self.inputs[2 * index]));
            assertions.push(Assertion::single(2, start, self.inputs[2 * index + 1]));
            assertions.push(Assertion::single(0, start + TOTAL_ROUNDS, *digest));
        }
        assertions
    }

    fn get_periodic_column_values(&self) -> Vec<Vec<Self::BaseField>> {
        let round_constants = hash::round_constants();
        let mut full_flag = vec![BaseElement::ZERO; ROUNDS_PER_HASH];
        let mut rc0 = vec![BaseElement::ZERO; ROUNDS_PER_HASH];
        let mut rc1 = vec![BaseElement::ZERO; ROUNDS_PER_HASH];
        let mut rc2 = vec![BaseElement::ZERO; ROUNDS_PER_HASH];
        let mut boundary_flag = vec![BaseElement::ZERO; ROUNDS_PER_HASH];

        for round in 0..TOTAL_ROUNDS {
            if hash::is_full_round(round) {
                full_flag[round] = BaseElement::ONE;
            }
            rc0[round] = round_constants[round][0];
            rc1[round] = round_constants[round][1];
            rc2[round] = round_constants[round][2];
        }
        boundary_flag[ROUNDS_PER_HASH - 1] = BaseElement::ONE;

        vec![full_flag, rc0, rc1, rc2, boundary_flag]
    }
}
