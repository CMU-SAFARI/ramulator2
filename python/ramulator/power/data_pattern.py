"""Data-activity knobs of Ayna's data-pattern model."""

from dataclasses import dataclass


@dataclass(frozen=True)
class DataPattern:
    """Toggle rates in [0, 1] on the three buses Ayna's data-pattern model distinguishes.

    dq_rate:  DQ-pin toggles between beats.
    tsv_rate: 2-bit on-die (TSV) toggles.
    bg_rate:  burst-to-burst inversions on the bank-group bus.

    0.5 on each is uniform-random data (the default); 0 on each is static data, the reference
    at which Ayna's device configs give IDD4R and IDD4W.
    """

    dq_rate: float = 0.5
    tsv_rate: float = 0.5
    bg_rate: float = 0.5

    def __post_init__(self):
        for name in ("dq_rate", "tsv_rate", "bg_rate"):
            value = getattr(self, name)
            if not 0.0 <= value <= 1.0:
                raise ValueError(f"DataPattern.{name} must be in [0, 1], got {value}")

    @classmethod
    def random(cls):
        return cls(0.5, 0.5, 0.5)

    @classmethod
    def static(cls):
        return cls(0.0, 0.0, 0.0)

    def knobs(self):
        return {
            "dq_rate": float(self.dq_rate),
            "tsv_rate": float(self.tsv_rate),
            "bg_rate": float(self.bg_rate),
        }
