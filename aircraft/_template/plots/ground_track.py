"""The ground track of every vehicle: north against east displacement from the start, for
ground_track.yaml (astra.plot's escape hatch, docs/plans/m6-pr5-plots.md).

The displacement integrates the derived NED velocity (``v<i>.derived.v_n_0`` and ``v_n_1``,
ADR-0026) over each logged row, holding each row's velocity until the next row, so it works for
every Earth model (``p_n`` is NED meters on a flat Earth but latitude and longitude on a round
one). It is a local flat-Earth picture of the path, not a geodetic track.
"""

from __future__ import annotations

import numpy as np

from astra import log, plot


def displacement(t: np.ndarray, velocity: np.ndarray) -> np.ndarray:
    """Displacement at each row from the start: the sum of velocity times step over earlier rows."""
    steps = velocity[:-1] * np.diff(t)
    return np.concatenate(([0.0], np.cumsum(steps)))


def figures(run_log: log.RunLog) -> list[plot.Figure]:
    series: list[plot.Series] = []
    for vehicle in plot.vehicles(run_log):
        t, north_velocity = plot.column(run_log, f"v{vehicle}.derived.v_n_0")
        _, east_velocity = plot.column(run_log, f"v{vehicle}.derived.v_n_1")
        north = displacement(t, north_velocity)
        east = displacement(t, east_velocity)
        series.append(plot.Series(f"v{vehicle}", east, north))
    panel = plot.Panel(title="", ylabel="north", unit="m", series=series, xlabel="east", xunit="m")
    return [plot.Figure("ground_track", "Ground track", [panel])]
