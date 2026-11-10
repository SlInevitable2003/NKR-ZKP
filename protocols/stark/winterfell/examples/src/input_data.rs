use core::{fmt, str::FromStr};

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum DataMode {
    #[default]
    Dense,
    Sparse,
    Hybrid,
}

impl FromStr for DataMode {
    type Err = String;

    fn from_str(name: &str) -> Result<Self, Self::Err> {
        match name {
            "dense" => Ok(Self::Dense),
            "sparse" => Ok(Self::Sparse),
            "hybrid" => Ok(Self::Hybrid),
            _ => Err(format!("unknown data mode: {name} (expected dense, sparse, or hybrid)")),
        }
    }
}

impl fmt::Display for DataMode {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Dense => "dense",
            Self::Sparse => "sparse",
            Self::Hybrid => "hybrid",
        })
    }
}

#[cfg(feature = "std")]
impl DataMode {
    pub fn generate(self, count: usize) -> Vec<winterfell::math::fields::f128::BaseElement> {
        use rand_utils::{rand_value, rand_vector, shuffle};
        use winterfell::math::{fields::f128::BaseElement, FieldElement};

        let mut values = rand_vector::<BaseElement>(count);
        for value in &mut values {
            while *value == BaseElement::ZERO {
                *value = rand_value();
            }
        }
        if self != Self::Dense {
            let dense_count = if self == Self::Hybrid { count - count / 2 } else { 0 };
            let zero_count = (count - dense_count) / 2;
            values[count - zero_count..].fill(BaseElement::ZERO);
            shuffle(&mut values[dense_count..]);
        }
        values
    }
}

#[cfg(all(test, feature = "std"))]
mod tests {
    use winterfell::math::{fields::f128::BaseElement, FieldElement};

    use super::DataMode;

    #[test]
    fn modes_have_exact_zero_counts_and_dense_prefixes() {
        for mode in [DataMode::Dense, DataMode::Sparse, DataMode::Hybrid] {
            assert_eq!(mode.to_string().parse::<DataMode>(), Ok(mode));
            for count in 0..=65 {
                let values = mode.generate(count);
                let dense_count = if mode == DataMode::Hybrid { count - count / 2 } else { 0 };
                let expected = if mode == DataMode::Dense {
                    0
                } else {
                    (count - dense_count) / 2
                };
                assert_eq!(values.len(), count);
                assert_eq!(
                    values.iter().filter(|&&value| value == BaseElement::ZERO).count(),
                    expected
                );
                assert!(values[..dense_count].iter().all(|&value| value != BaseElement::ZERO));
            }
        }
    }

    #[test]
    fn invalid_modes_are_rejected() {
        for name in ["", "Dense", "unknown"] {
            assert!(name.parse::<DataMode>().is_err());
        }
    }

    #[test]
    fn command_line_accepts_modes_before_and_after_the_circuit() {
        use structopt::StructOpt;

        use crate::{ExampleOptions, ExampleType};

        assert_eq!(
            ExampleOptions::from_iter_safe(["winterfell", "fib"]).unwrap().data_mode(),
            DataMode::Dense
        );
        for name in ["dense", "sparse", "hybrid"] {
            for circuit in ["matrix-mul", "matmul", "fib", "fibonacci"] {
                for arguments in [
                    vec!["winterfell", "--data-mode", name, circuit, "-n", "16", "--profile"],
                    vec!["winterfell", circuit, "-n", "16", "--data-mode", name, "--profile"],
                ] {
                    let options = ExampleOptions::from_iter_safe(arguments).unwrap();
                    assert_eq!(options.data_mode(), name.parse::<DataMode>().unwrap());
                    assert!(options.profile_enabled());
                    assert!(matches!(
                        options.example,
                        ExampleType::Fib { .. } | ExampleType::MatrixMul { .. }
                    ));
                }
            }
        }
        assert!(ExampleOptions::from_iter_safe(["winterfell", "fib", "--data-mode", "invalid"])
            .is_err());
        assert!(ExampleOptions::from_iter_safe(["winterfell", "fib", "--data-mode"]).is_err());
    }
}
