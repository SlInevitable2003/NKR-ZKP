// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use winterfell::{
    math::ToElements, Air, AirContext, Assertion, EvaluationFrame, ProofOptions, TraceInfo,
    TransitionConstraintDegree,
};

use super::{BaseElement, FieldElement, TRACE_WIDTH};

// PUBLIC INPUTS
// ================================================================================================

/// Public inputs for the matrix multiplication example: the two input matrices `a` and `b`, and
/// their product `c`, all stored in row-major order.
#[derive(Clone)]
pub struct PublicInputs {
    pub a: Vec<BaseElement>,
    pub b: Vec<BaseElement>,
    pub c: Vec<BaseElement>,
}

impl ToElements<BaseElement> for PublicInputs {
    fn to_elements(&self) -> Vec<BaseElement> {
        let mut result = Vec::with_capacity(self.a.len() + self.b.len() + self.c.len());
        result.extend_from_slice(&self.a);
        result.extend_from_slice(&self.b);
        result.extend_from_slice(&self.c);
        result
    }
}

// MATRIX MULTIPLICATION AIR
// ================================================================================================

/// Describes the AIR for proving that `c = a * b` for two `n x n` matrices `a` and `b`.
///
/// The execution trace has four columns laid out over `n^3` steps:
///
/// * column 0 (`a`): the current element `a[i][k]` of the left operand;
/// * column 1 (`b`): the current element `b[k][j]` of the right operand;
/// * column 2 (`acc`): the running dot-product accumulator for the current output `c[i][j]`;
/// * column 3 (`c`): the committed result `c[i][j]`, held constant for the `n` steps of a single
///   dot product.
///
/// Each output element `c[i][j]` is computed over `n` consecutive steps, one per inner index `k`.
/// A periodic column `is_last` of length `n` (with a single `1` at position `n - 1`) marks the
/// final step of each dot product and is used to reset the accumulator and to check the result.
pub struct MatrixMulAir {
    context: AirContext<BaseElement>,
    matrix_size: usize,
    a: Vec<BaseElement>,
    b: Vec<BaseElement>,
    c: Vec<BaseElement>,
}

impl Air for MatrixMulAir {
    type BaseField = BaseElement;
    type PublicInputs = PublicInputs;

    // CONSTRUCTOR
    // --------------------------------------------------------------------------------------------
    fn new(trace_info: TraceInfo, pub_inputs: PublicInputs, options: ProofOptions) -> Self {
        assert_eq!(TRACE_WIDTH, trace_info.width());

        // trace length is n^3, so the matrix size n is the cube root of the trace length
        let matrix_size = 1usize << (trace_info.length().trailing_zeros() / 3);
        assert_eq!(
            matrix_size * matrix_size * matrix_size,
            trace_info.length(),
            "trace length must be a perfect cube"
        );
        assert_eq!(pub_inputs.a.len(), matrix_size * matrix_size);
        assert_eq!(pub_inputs.b.len(), matrix_size * matrix_size);
        assert_eq!(pub_inputs.c.len(), matrix_size * matrix_size);

        // transition constraints:
        // 1. accumulator update / reset;
        // 2. result check on the last step of each dot product;
        // 3. result register held constant within a dot product.
        //
        // constraints 1 and 2 involve the multiplication of two trace columns (`a` and `b`) and
        // the periodic column `is_last`, so their degree is described as base 2 with a single
        // cycle of length n; constraint 3 involves a single trace column and the periodic column.
        let degrees = vec![
            TransitionConstraintDegree::with_cycles(2, vec![matrix_size]),
            TransitionConstraintDegree::with_cycles(2, vec![matrix_size]),
            TransitionConstraintDegree::with_cycles(1, vec![matrix_size]),
        ];

        // one assertion to initialize the accumulator, `n` assertions for each of columns `a` and
        // `b`, and one assertion for the result column `c`.
        let num_assertions = 2 + 2 * matrix_size;

        MatrixMulAir {
            context: AirContext::new(trace_info, degrees, num_assertions, options),
            matrix_size,
            a: pub_inputs.a,
            b: pub_inputs.b,
            c: pub_inputs.c,
        }
    }

    fn context(&self) -> &AirContext<Self::BaseField> {
        &self.context
    }

    fn evaluate_transition<E: FieldElement + From<Self::BaseField>>(
        &self,
        frame: &EvaluationFrame<E>,
        periodic_values: &[E],
        result: &mut [E],
    ) {
        let current = frame.current();
        let next = frame.next();
        debug_assert_eq!(TRACE_WIDTH, current.len());
        debug_assert_eq!(TRACE_WIDTH, next.len());

        let a = current[0];
        let b = current[1];
        let acc = current[2];
        let c = current[3];
        let is_last = periodic_values[0];

        // when not on the last step of a dot product, accumulate the product a * b; on the last
        // step, reset the accumulator to ZERO for the next dot product.
        result[0] = next[2] - (E::ONE - is_last) * (acc + a * b);

        // on the last step, the accumulator plus the current product must equal the committed
        // result c[i][j].
        result[1] = is_last * (acc + a * b - c);

        // within a dot product, the committed result must remain unchanged.
        result[2] = (E::ONE - is_last) * (next[3] - c);
    }

    fn get_assertions(&self) -> Vec<Assertion<Self::BaseField>> {
        let n = self.matrix_size;
        let mut assertions = Vec::with_capacity(2 + 2 * n);

        // the accumulator starts at ZERO.
        assertions.push(Assertion::single(2, 0, BaseElement::ZERO));

        // bind column 0 to matrix `a`. For each inner index `k`, the values `a[i][k]` are read in
        // order for every `j`, so we assert them as a sequence of `n^2` values with stride `n`
        // starting at offset `k`.
        for k in 0..n {
            let mut values = Vec::with_capacity(n * n);
            for i in 0..n {
                for _ in 0..n {
                    values.push(self.a[i * n + k]);
                }
            }
            assertions.push(Assertion::sequence(0, k, n, values));
        }

        // bind column 1 to matrix `b`. Because of the rotated dot-product order
        // `j = (jp + i) % n`, the value at stride-`n` step `i * n + jp` (offset `k`) is
        // `b[k][(jp + i) % n]`.
        for k in 0..n {
            let mut values = Vec::with_capacity(n * n);
            for i in 0..n {
                for jp in 0..n {
                    values.push(self.b[k * n + ((jp + i) % n)]);
                }
            }
            assertions.push(Assertion::sequence(1, k, n, values));
        }

        // bind column 3 to the result matrix `c`. The result `c[i][(jp + i) % n]` is held constant
        // over the `n` steps of its dot product, so a sequence of `n^2` values with stride `n`
        // suffices.
        let mut c_values = Vec::with_capacity(n * n);
        for i in 0..n {
            for jp in 0..n {
                c_values.push(self.c[i * n + ((jp + i) % n)]);
            }
        }
        assertions.push(Assertion::sequence(3, 0, n, c_values));

        assertions
    }

    fn get_periodic_column_values(&self) -> Vec<Vec<Self::BaseField>> {
        // a single periodic column of length n that is ONE at the last step of each dot product
        // and ZERO everywhere else.
        let mut is_last = vec![BaseElement::ZERO; self.matrix_size];
        is_last[self.matrix_size - 1] = BaseElement::ONE;
        vec![is_last]
    }
}
