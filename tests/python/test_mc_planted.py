"""M6 R9, the planted cause in the notional transport, and its negative control (C-08)
(docs/plans/m6-pr7b-planted.md; docs/plans/m6-analysis-tooling.md R9, Q9, Q15;
docs/plans/m6-pr7-mc-sizing.md D8, D11). Every test is named ``test_planted_*``, so
``pytest tests/python -k planted`` selects them.

The protocol (PR 7's D8) runs in one module fixture, every time: the two pilots (master seed 7071)
are sized with ``astra.mc_sizing``'s ranking option (the top driver for the main pilot, null at
r_min 0.3 for the control) and deleted; then the final batches (master seed 707) run with the
``runs`` their specs commit, which the sizing must reproduce. Everything is removed at the end of
the module, on failure too. The final batches carry R9's verdict: KS and Spearman rank the cause
first with the minimum effect and margin stated in ``planted.yaml``'s header, and the control flags
nothing (C-08). The KS statistic of the cause is also recomputed from the summary with scipy.

R9's config also goes through ``astra.montecarlo``'s PyYAML route (D4): a one-run batch of the base
config must give the same rate-group data as the file read by yaml-cpp.
"""

from __future__ import annotations

import dataclasses
import shutil
from collections.abc import Iterator
from pathlib import Path
from typing import Any, cast

import pandas as pd
import pytest
import yaml

from astra import _core, log, mc_analysis, mc_sizing, montecarlo

REPO = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "mc_planted_fixtures"
BASE = REPO / "aircraft/notional_transport/configs/cruise.yaml"
MAIN_PILOT = FIXTURES / "planted_pilot.yaml"
CONTROL_PILOT = FIXTURES / "planted_control_pilot.yaml"
MAIN = FIXTURES / "planted.yaml"
CONTROL = FIXTURES / "planted_control.yaml"

CAUSE = "vehicle.models.0.cm_alpha_per_rad"
METRIC = "max.v0.derived.alpha"
CRITERION = f"{METRIC} > 0.23"  # rad; the rationale is in planted.yaml's header
PILOT_SEED = 7071
FINAL_SEED = 707
R_MIN = "0.3"
RANKED = 4  # the cause and three inert coefficients; the wind seed is never ranked
MIN_D = 0.5
MIN_RHO = 0.5
MARGIN = 0.25
Q = 0.05
MAX_RUNS = 357  # 2 x N x 5.6 MB of logs above this exceeds 4 GB (PR 7's D11): Joe's call


# --- the protocol --------------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class Pilot:
    runs: int
    seed: int | None
    check: mc_sizing.Check
    deleted: bool


@dataclasses.dataclass(frozen=True)
class Planted:
    main_pilot: Pilot
    control_pilot: Pilot
    main: mc_analysis.Batch
    control: mc_analysis.Batch


def _pilot(spec_path: Path, claim: mc_sizing.RankingClaim, out: Path) -> Pilot:
    """Runs a pilot, sizes from its summary alone, and deletes it (D8: a pilot only sizes)."""
    spec = montecarlo.load(spec_path, root=REPO)
    montecarlo.run(spec, out)
    batch = mc_analysis.load_batch(out)
    check = mc_sizing.check_one(batch, claim)
    seed = mc_sizing.batch_seed(batch)
    shutil.rmtree(out)
    return Pilot(spec.runs, seed, check, not out.exists())


def _final(spec_path: Path, out: Path) -> mc_analysis.Batch:
    montecarlo.run(montecarlo.load(spec_path, root=REPO), out)
    return mc_analysis.load_batch(out)


@pytest.fixture(scope="module")
def planted(tmp_path_factory: pytest.TempPathFactory) -> Iterator[Planted]:
    root = tmp_path_factory.mktemp("planted")
    try:
        main_pilot = _pilot(MAIN_PILOT, mc_sizing.RankingClaim(METRIC, "top"), root / "main_pilot")
        control_pilot = _pilot(
            CONTROL_PILOT, mc_sizing.RankingClaim(METRIC, "null", R_MIN), root / "control_pilot"
        )
        main = _final(MAIN, root / "main")
        control = _final(CONTROL, root / "control")
        yield Planted(main_pilot, control_pilot, main, control)
    finally:
        shutil.rmtree(root, ignore_errors=True)


def _spec_text(path: Path) -> dict[str, Any]:
    return cast(dict[str, Any], yaml.safe_load(path.read_text(encoding="utf-8")))


def _column(batch: mc_analysis.Batch, name: str) -> list[Any]:
    return [batch.value(name, row) for row in range(len(batch))]


# --- the specs -----------------------------------------------------------------------------------


def test_planted_specs_agree() -> None:
    main_pilot = _spec_text(MAIN_PILOT)
    control_pilot = _spec_text(CONTROL_PILOT)
    main = _spec_text(MAIN)
    control = _spec_text(CONTROL)

    # A pilot is its final batch with another master seed and the pilot's run count.
    for pilot, final in ((main_pilot, main), (control_pilot, control)):
        assert pilot["seed"] == PILOT_SEED
        assert final["seed"] == FINAL_SEED
        assert pilot["runs"] == mc_sizing.ranking_runs(R_MIN, RANKED) == 120
        assert {**pilot, "seed": FINAL_SEED, "runs": final["runs"]} == final
    assert main["runs"] == control["runs"]

    # The control is the main batch without the cause, which is drawn last, so every run index
    # draws the same inert values and wind seed in both.
    dispersions = list(main["dispersions"].items())
    assert dispersions[-1][0] == CAUSE
    assert dispersions[-1][1] == {"uniform": {"lo": -0.6, "hi": 0.6}}
    assert list(control["dispersions"].items()) == dispersions[:-1]
    assert {**control, "dispersions": main["dispersions"]} == main

    specs = [montecarlo.load(path, root=REPO) for path in (MAIN, CONTROL)]
    for index in (0, 1, 129):
        main_draw = montecarlo.draw(specs[0], index)[1]
        control_draw = montecarlo.draw(specs[1], index)[1]
        assert {key: main_draw[key] for key in control_draw} == control_draw


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
