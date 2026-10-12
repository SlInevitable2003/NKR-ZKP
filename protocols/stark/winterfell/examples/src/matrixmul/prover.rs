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
    BaseElement, DefaultRandomCoin, ElementHasher, FieldElement, MatrixMulAir, PhantomData,
    ProofOptions, Prover, PublicInputs,
};

// MATRIX MULTIPLICATION PROVER
// ================================================================================================

pub struct MatrixMulProver<H: ElementHasher> {
    options: ProofOptions,
    _hasher: PhantomData<H>,
}

impl<H: ElementHasher> MatrixMulProver<H> {
    pub fn new(options: ProofOptions) -> Self {
        Self { options, _hasher: PhantomData }
    }

    /// Builds an execution trace for computing `c = a * b`, where `a`, `b`, and `c` are `n x n`
    /// matrices stored in row-major order.
    ///
    /// Each row of the trace advances the inner dot-product loop by a single step; the trace is
    /// laid out as [a, b, acc, c] over `n^3` steps.
    ///
    /// For every output row `i`, the `n` dot products are computed in the order `j = (jp + i) % n`
    /// (rather than the natural `0..n` order). This rotation makes column `b` depend on the outer
    /// index `i` as well as on `jp` and `k`, so that every trace column attains the full polynomial
    /// degree expected by the AIR.
    pub fn build_trace(
        &self,
        a: &[BaseElement],
        b: &[BaseElement],
        c: &[BaseElement],
        n: usize,
    ) -> TraceTable<BaseElement> {
        assert!(n.is_power_of_two(), "matrix size must be a power of 2");

        let trace_length = n * n * n;
        let mut col_a = vec![BaseElement::ZERO; trace_length];
        let mut col_b = vec![BaseElement::ZERO; trace_length];
        let mut col_acc = vec![BaseElement::ZERO; trace_length];
        let mut col_c = vec![BaseElement::ZERO; trace_length];

        for i in 0..n {
            for jp in 0..n {
                let j = (jp + i) % n;
                let mut acc = BaseElement::ZERO;
                for k in 0..n {
                    let step = (i * n + jp) * n + k;
                    let av = a[i * n + k];
                    let bv = b[k * n + j];
                    col_a[step] = av;
                    col_b[step] = bv;
                    col_acc[step] = acc;
                    col_c[step] = c[i * n + j];
                    acc += av * bv;
                }
            }
        }

        TraceTable::init(vec![col_a, col_b, col_acc, col_c])
    }
}

impl<H: ElementHasher> Prover for MatrixMulProver<H>
where
    H: ElementHasher<BaseField = BaseElement> + Sync,
{
    type BaseField = BaseElement;
    type Air = MatrixMulAir;
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
        // recover the input and output matrices from the trace: column 0 holds `a`, column 1
        // holds `b`, and column 3 holds `c`, each read at the appropriate offsets.
        let n = 1usize << (trace.length().trailing_zeros() / 3);

        let mut a = vec![BaseElement::ZERO; n * n];
        let mut b = vec![BaseElement::ZERO; n * n];
        let mut c = vec![BaseElement::ZERO; n * n];

        for i in 0..n {
            for k in 0..n {
                a[i * n + k] = trace.get(0, i * n * n + k);
            }
            for jp in 0..n {
                let j = (jp + i) % n;
                c[i * n + j] = trace.get(3, (i * n + jp) * n);
            }
        }
        for k in 0..n {
            for j in 0..n {
                b[k * n + j] = trace.get(1, j * n + k);
            }
        }

        PublicInputs { a, b, c }
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
