"""M6 R9, the planted cause in the notional transport, and its negative control (C-08)
(docs/plans/m6-pr7b-planted.md; docs/plans/m6-analysis-tooling.md R9, Q9, Q15;
docs/plans/m6-pr7-mc-sizing.md D8, D11). Every test is named ``test_planted_*``, so
``pytest tests/python -k planted`` selects them.

R9's config also goes through ``astra.montecarlo``'s PyYAML route (D4): a one-run batch of the base
config must give the same rate-group data as the file read by yaml-cpp.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, cast

import pandas as pd

from astra import _core, log, montecarlo

REPO = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "mc_planted_fixtures"
BASE = REPO / "aircraft/notional_transport/configs/cruise.yaml"


# --- D4: R9's config through the PyYAML route ----------------------------------------------------


def test_planted_r9_config_pyyaml_route_matches_yaml_cpp(tmp_path: Path) -> None:
    spec = montecarlo.load(FIXTURES / "planted_route.yaml", root=REPO)
    assert spec.config_path == BASE
    batch = tmp_path / "batch"
    summary = montecarlo.run(spec, batch, workers=1)
    assert list(summary["status"]) == ["ok"]
    routed = log.load(batch / montecarlo.log_name(0))

    # yaml-cpp reads cruise.yaml's own bytes from a file; only a log block is appended.
    direct_path = tmp_path / "direct.h5"
    config_file = tmp_path / "cruise_logged.yaml"
    config_file.write_bytes(BASE.read_bytes() + f"\nlog:\n  path: {direct_path}\n".encode())
    sim = _core.Simulation.from_config(
        _core.Config.from_file(config_file), _core.default_plugin_dir()
    )
    sim.run(spec.frames)
    sim.close_log()
    direct = log.load(direct_path)

    assert routed.frames_run == direct.frames_run == spec.frames
    assert routed.halt is None
    assert direct.halt is None
    assert routed.units == direct.units
    assert routed.rates.keys() == direct.rates.keys()
    for divisor, frame in direct.rates.items():
        pd.testing.assert_frame_equal(routed.rates[divisor], frame, check_exact=True)
    for name, state in direct.final_state.items():
        pd.testing.assert_series_equal(routed.final_state[name], state, check_exact=True)
    pd.testing.assert_series_equal(
        cast(Any, routed.maximum), cast(Any, direct.maximum), check_exact=True
    )
    del sim
