use ark_bls12_381::{Bls12_381, Fr as BlsScalar, G1Affine};
use ark_ec::{AffineCurve, ProjectiveCurve, TEModelParameters};
use ark_ed_on_bls12_381::{
    EdwardsParameters as JubJubParameters, Fr as JubJubScalar,
};
use ark_ff::PrimeField;
use ark_poly::polynomial::univariate::DensePolynomial;
use ark_poly_commit::{sonic_pc::SonicKZG10, PolynomialCommitment};
use plonk::error::to_pc_error;
use plonk_core::circuit::{verify_proof, Circuit};
use plonk_core::commitment::KZG10;
use plonk_core::constraint_system::StandardComposer;
use plonk_core::proof_system::Prover;
use plonk_core::permutation::MSM_KERN;
use plonk_core::error::Error;
use plonk_core::prelude::*;
use rand_core::OsRng;

use ec_gpu_common::api::{*};
use std::os::raw::c_void;
use std::os::raw::c_int;
use ec_gpu_common::G1AffineNoInfinity;


pub const MATRIX_N: usize = 5;


#[derive(derivative::Derivative)]
#[derivative(Debug(bound = ""), Default(bound = ""))]
pub struct MatrixMultiplicationCircuit<F>
where
    F: PrimeField,
{
    matrix_a: Vec<Vec<F>>,
    matrix_b: Vec<Vec<F>>,
    result_matrix: Vec<Vec<F>>,
}

impl<F, P> Circuit<F, P> for MatrixMultiplicationCircuit<F>
where
    F: PrimeField,
    P: TEModelParameters<BaseField = F>,
{
    const CIRCUIT_ID: [u8; 32] = [0xff; 32];

    fn gadget(
        &mut self,
        composer: &mut StandardComposer<F, P>,
    ) -> Result<(), Error> {
        let rows_a = self.matrix_a.len();
        let cols_a = if self.matrix_a.is_empty() {
            0
        } else {
            self.matrix_a[0].len()
        };
        let cols_b = if self.matrix_b.is_empty() {
            0
        } else {
            self.matrix_b[0].len()
        };
        println!("row:{}, col:{}", rows_a, cols_a);

        if rows_a == 0 || cols_a == 0 || cols_b == 0 {
            // return Err(Error::Other("Matrix is empty".to_string()));
        }

        for i in 0..rows_a {
            for j in 0..cols_b {
                let mut sum = composer.zero_var();
                for k in 0..cols_a {
                    let a = composer.add_input(self.matrix_a[i][k]);
                    let b = composer.add_input(self.matrix_b[k][j]);
                    // 替换 mul 为 arithmetic_gate 实现乘法
                    let mul_result = composer.arithmetic_gate(|gate| {
                        gate.witness(a, b, None).mul(F::one())
                    });
                    // 替换 add 为 arithmetic_gate 实现加法
                    sum = composer.arithmetic_gate(|gate| {
                        gate.witness(sum, mul_result, None).add(F::one(), F::one())
                    });
                }
                let expected = composer.add_input(self.result_matrix[i][j]);
                composer.assert_equal(sum, expected);
            }
        }

        Ok(())
    }

    fn padded_circuit_size(&self) -> usize {
        // 根据矩阵规模调整
        // 1 << (MATRIX_N)
        1<<9
    }
}


fn main() -> Result<(), Error> {
    // 定义两个简单的矩阵
    // let matrix_a: Vec<Vec<BlsScalar>> = vec![
    //     vec![BlsScalar::from(1u64), BlsScalar::from(2u64)],
    //     vec![BlsScalar::from(3u64), BlsScalar::from(4u64)],
    // ];
    // let matrix_b: Vec<Vec<BlsScalar>> = vec![
    //     vec![BlsScalar::from(5u64), BlsScalar::from(6u64)],
    //     vec![BlsScalar::from(7u64), BlsScalar::from(8u64)],
    // ];

    let matrix_a: Vec<Vec<BlsScalar>> = vec![
        vec![BlsScalar::from(1u64); MATRIX_N];
        MATRIX_N
    ];
    let matrix_b: Vec<Vec<BlsScalar>> = vec![
        vec![BlsScalar::from(1u64); MATRIX_N];
        MATRIX_N
    ];

    // println!("row:{}, col:{}", matrix_a.len(), matrix_a[0].len());

    // 计算矩阵乘法的结果
    let rows_a = matrix_a.len();
    let cols_a = matrix_a[0].len();
    let cols_b = matrix_b[0].len();
    let mut result_matrix = vec![vec![BlsScalar::from(0u64); cols_b]; rows_a];
    for i in 0..rows_a {
        for j in 0..cols_b {
            for k in 0..cols_a {
                result_matrix[i][j] += matrix_a[i][k] * matrix_b[k][j];
            }
        }
    }

    // 创建电路实例
    let mut circuit: MatrixMultiplicationCircuit<BlsScalar> = MatrixMultiplicationCircuit {
        matrix_a,
        matrix_b,
        result_matrix,
    };

    // Generate CRS
    // type PC = SonicKZG10<Bls12_381, DensePolynomial<BlsScalar>>;
    type PC = KZG10<Bls12_381>;
    let pp = KZG10::<Bls12_381>::setup(1<<10, None, &mut OsRng)
        .map_err(to_pc_error::<BlsScalar, PC>)?;

    // Compile the circuit
    let (pk_p, (vk, _pi_pos)) = <MatrixMultiplicationCircuit<BlsScalar> as Circuit<BlsScalar, JubJubParameters>>::compile::<PC>(&mut circuit, &pp)?;

    let coeffs_count = 1 << 9;
    let (ck, _) = <KZG10<Bls12_381>>::trim(&pp, coeffs_count, 0, None).unwrap();
    let gpu_context = <KZG10<Bls12_381>>::get_gpu_context(&MSM_KERN, &ck, coeffs_count);

    // 生成证明
    let (proof, pi) = {
        let mut prover =
            Prover::<BlsScalar, JubJubParameters, KZG10<Bls12_381>>::new(
                b"Matrix Mul",
            );
        // circuit.gadget(prover.mut_cs()).unwrap();
        prover.mut_cs().check_circuit_satisfied();
        <MatrixMultiplicationCircuit<BlsScalar> as Circuit<BlsScalar, JubJubParameters>>::gen_proof::<KZG10<Bls12_381>, G1Affine>(&mut circuit, &pp, pk_p, b"Matrix Mul", Some(&gpu_context))?
    };
    // let (proof, pi) = <MatrixMultiplicationCircuit<BlsScalar> as Circuit<BlsScalar, JubJubParameters>>::gen_proof::<PC>(&mut circuit, &pp, pk_p, b"MatrixMultiplication")?;

    // 验证证明
    let verifier_data = VerifierData::new(vk, pi);
    let res = verify_proof::<BlsScalar, JubJubParameters, PC>(
        &pp,
        verifier_data.key,
        &proof,
        &verifier_data.pi,
        b"Matrix Mul",
    );

    println!("矩阵乘法电路证明生成和验证{}！", res.is_ok());
    res
}