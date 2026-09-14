"""ASTRA: a vehicle-agnostic 6-DoF simulation framework.

``astra._core`` is the nanobind module over ``libastra_core`` (docs/architecture.md §12). The
pure-Python tools (trim, linearize, montecarlo, log, scenario, fgbridge) arrive with later
milestones.
"""

from astra import _core

__version__: str = _core.VERSION

__all__ = ["__version__"]
