use core::{fmt, str::FromStr};

/// Input density for the matrix-multiplication example.
///
/// `Full` keeps every entry random and nonzero, `Hybrid` keeps 50% of the
/// entries nonzero, and `Sparse` keeps 10%. The remaining entries are set to
/// zero at shuffled positions; the matrices are sampled independently.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum Density {
    #[default]
    Full,
    Hybrid,
    Sparse,
}

impl FromStr for Density {
    type Err = String;

    fn from_str(name: &str) -> Result<Self, Self::Err> {
        match name.to_ascii_lowercase().as_str() {
            "full" => Ok(Self::Full),
            "hybrid" => Ok(Self::Hybrid),
            "sparse" => Ok(Self::Sparse),
            _ => Err(format!("unknown density: {name} (expected full, hybrid, or sparse)")),
        }
    }
}

impl fmt::Display for Density {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Full => "full",
            Self::Hybrid => "hybrid",
            Self::Sparse => "sparse",
        })
    }
}

#[cfg(feature = "std")]
impl Density {
    /// Number of nonzero entries for `count` inputs: `full` keeps all,
    /// `hybrid` keeps `ceil(count / 2)`, and `sparse` keeps `ceil(count / 10)`.
    pub const fn nonzero_count(self, count: usize) -> usize {
        match self {
            Self::Full => count,
            Self::Hybrid => count - count / 2,
            Self::Sparse => count / 10 + if count % 10 == 0 { 0 } else { 1 },
        }
    }

    /// Builds `count` random field elements and zeroes the masked positions.
    pub fn generate(self, count: usize) -> Vec<winterfell::math::fields::f128::BaseElement> {
        use winterfell::math::{fields::f128::BaseElement, FieldElement};

        let mut values = rand_utils::rand_vector::<BaseElement>(count);
        for value in &mut values {
            while *value == BaseElement::ZERO {
                *value = rand_utils::rand_value();
            }
        }

        let nonzero = self.nonzero_count(count);
        if nonzero < count {
            rand_utils::shuffle(&mut values);
            values[nonzero..].fill(BaseElement::ZERO);
            rand_utils::shuffle(&mut values);
        }
        values
    }
}

#[cfg(all(test, feature = "std"))]
mod tests {
    use super::Density;
    use winterfell::math::{fields::f128::BaseElement, FieldElement};

    #[test]
    fn levels_have_exact_nonzero_ratios() {
        for density in [Density::Full, Density::Hybrid, Density::Sparse] {
            assert_eq!(density.to_string().parse::<Density>(), Ok(density));
            for count in 0..=101 {
                let values = density.generate(count);
                let len = values.len();
                let zeros = values.iter().filter(|&&value| value == BaseElement::ZERO).count();
                assert_eq!(len, count);
                assert_eq!(count - zeros, density.nonzero_count(count));
            }
        }
    }

    #[test]
    fn names_are_case_insensitive_and_invalid_ones_are_rejected() {
        for (name, expected) in [
            ("full", Density::Full),
            ("FULL", Density::Full),
            ("Hybrid", Density::Hybrid),
            ("SPARSE", Density::Sparse),
        ] {
            assert_eq!(name.parse::<Density>(), Ok(expected));
        }
        for name in ["", "dense", "50%", "hybrid "] {
            assert!(name.parse::<Density>().is_err());
        }
    }
}
