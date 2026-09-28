"""astra.fgbridge: FlightGear FGNetFDM version 24 packets from a run (M5 PR 8; ADR-0002, ADR-0010,
ADR-0026; docs/plans/m5-tooling.md Q21 to Q23, R18 to R20).

Each packet is one frame's derived flight condition (``v<i>.derived.*``, ADR-0026) in FlightGear's
native FDM layout: 408 bytes, network byte order, ``version = 24``. It is sent over UDP (default
``127.0.0.1:5500``), which ``fgfs --native-fdm=socket,in,<hz>,,5500,udp`` reads. Two sources send
the same bytes for the same run:

- ``LiveBridge``: a ``Simulation`` the caller steps through ``LiveBridge.step()`` or
  ``LiveBridge.run()``, which also drain the run log after every step, as ``Simulation.run()``
  and ``astra.lockstep.Session`` do (ADR-0025);
- ``send_log``: a complete run log (ADR-0025), read with ``astra.log.load`` strictly, never with
  ``allow_incomplete``.

Frame k's values. Both sources use one definition: row k of the run log, which holds frame k's
frame-start values at t_k (ADR-0025). These are also what the registry's derived signals hold once
``step()`` has completed frame k. A halted step completes no frame, so it has no log row and no
packet.

Rate (Q23). One packet for each frame with ``frame % divisor == 0``, carrying that frame's values.
The default divisor is 16 (62.5 Hz at dt = 1 ms), and any integer divisor >= 1 is accepted.
Pacing to wall time (``pace=True``) is Python-only and off by default. It holds the packet for
frame k until ``start + (k - k0) * dt`` on ``time.monotonic()``, where frame k0 is the first packet
sent. It only delays sending and never feeds simulation time (ADR-0010's scope is ``core/``,
``plugins/`` and ``aircraft/``).

Units. Every source value is SI (ADR-0002). Conversion happens here and nowhere else, to the unit
FlightGear reads each field in. The transcribed oracle is
``tests/golden/flightgear/fgnetfdm_v24.py`` (FlightGear 2024.1.7's ``net_fdm.hxx``, checked
against ``FGFDM2Props<FGNetFDM>``). Constants: 1 ft = 0.3048 m and 1 kt = 1852/3600 m/s, both
exact by definition.

Fields, in packet order, with their sources (``d.x`` is ``v<i>.derived.x``):

========================  =======  ======================================================
field                     type     value
========================  =======  ======================================================
version                   uint32   24
padding                   uint32   0
longitude                 double   d.lon, rad
latitude                  double   d.lat, rad
altitude                  double   d.altitude, m
agl                       float    d.altitude, m (no terrain, Q22)
phi, theta, psi           float    d.phi, d.theta, d.psi, rad (3-2-1 Euler, ADR-0001)
alpha, beta               float    d.alpha, d.beta, rad
phidot, thetadot, psidot  float    0 (Euler rates are not published, Q22)
vcas                      float    d.cas / kt, knots
climb_rate                float    d.vertical_speed / ft, ft/s, positive up
v_north, v_east, v_down   float    d.v_n_0, d.v_n_1, d.v_n_2 / ft, NED ground velocity, ft/s
v_body_u, v_body_v,       float    d.u, d.v, d.w / ft, ft/s: the Earth-relative body velocity
v_body_w                           v_b (the state's, as the ICD packs it), never air-relative
A_X_pilot, A_Y_pilot,     float    d.ax, d.ay, d.az / ft, ft/s^2: specific force at the CG,
A_Z_pilot                          frame k-1's (ADR-0026 timing exception); see below
stall_warning, slip_deg   float    0
num_engines               uint32   0 (FlightGear then applies no engine field)
eng_state[4]              uint32   0
rpm ... oil_px [4] each   float    0 (rpm, fuel_flow, fuel_px, egt, cht, mp_osi, tit,
                                   oil_temp, oil_px)
num_tanks                 uint32   0
fuel_quantity[4]          float    0
num_wheels                uint32   0
wow[3]                    uint32   0
gear_pos, gear_steer,     float    0
gear_compression [3] each
cur_time                  uint32   0
warp                      int32    0
visibility                float    0
elevator ... spoilers     float    0 (elevator, elevator_trim_tab, left_flap, right_flap,
                                   left_aileron, right_aileron, rudder, nose_wheel,
                                   speedbrake, spoilers; no surface range is declared)
========================  =======  ======================================================

Invalid values. A derived field whose ICD status bit is set holds the ICD's defined 0 (ADR-0026).
The packet carries that 0 converted, which is 0. The bridge defines one case explicitly:
while ``ASTRA_ICD_STATUS_ACCEL_INVALID`` (0x1) is set, which is always the case at frame 0,
``A_X_pilot``, ``A_Y_pilot`` and ``A_Z_pilot`` are exactly ``0.0`` whatever the source holds, and
every other field is packed normally.

Refusals (``BridgeError``). All of them happen before any packet is sent:
- any frame whose ``status_flags`` carries ``ASTRA_ICD_STATUS_LATLON_NOT_MODELED`` (0x2), a flat
  Earth, whose latitude and longitude are placeholders (R20). The log source checks every row
  first. The live source checks every completed frame, which refuses a flat-Earth run at its first
  frame: the registry holds no derived value before the first step;
- a source that lacks a derived signal the packet reads (a config without physics);
- a ``divisor`` that is not an ``int`` >= 1 (a ``bool`` is refused), or a log whose derived
  signals are logged at a rate that has no row at some due frame;
- for the live source, a ``Simulation`` whose clock moved outside the bridge, because the packets
  of those frames could never be sent.
"""

from __future__ import annotations

import socket
import struct
import time
from collections.abc import Mapping
from pathlib import Path
from types import TracebackType
from typing import Any, Self, cast

import numpy as np

from astra import _core, log

FG_NET_FDM_VERSION = 24
PACKET_SIZE = 408
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 5500
DEFAULT_DIVISOR = 16

# icd/astra_icd.h status_flags bits.
STATUS_ACCEL_INVALID = 0x1
STATUS_LATLON_NOT_MODELED = 0x2

FOOT = 0.3048  # m, exact
KNOT = 1852.0 / 3600.0  # m/s, exact

# The derived fields the packet reads, without the "v<i>.derived." prefix.
FIELDS = (
    "lon",
    "lat",
    "altitude",
    "phi",
    "theta",
    "psi",
    "alpha",
    "beta",
    "cas",
    "vertical_speed",
    "v_n_0",
    "v_n_1",
    "v_n_2",
    "u",
    "v",
    "w",
    "ax",
    "ay",
    "az",
    "status_flags",
)

# FGNetFDM v24, written by hand from net_fdm.hxx (not generated from the golden table, which the
# tests decode with independently). ">": every field big-endian, with no implicit padding.
_FORMAT = (
    ">"
    "II"  # version, padding
    "3d"  # longitude, latitude, altitude
    "6f"  # agl, phi, theta, psi, alpha, beta
    "3f"  # phidot, thetadot, psidot
    "2f"  # vcas, climb_rate
    "3f"  # v_north, v_east, v_down
    "3f"  # v_body_u, v_body_v, v_body_w
    "3f"  # A_X_pilot, A_Y_pilot, A_Z_pilot
    "2f"  # stall_warning, slip_deg
    "I4I"  # num_engines, eng_state[4]
    "36f"  # rpm, fuel_flow, fuel_px, egt, cht, mp_osi, tit, oil_temp, oil_px: [4] each
    "I4f"  # num_tanks, fuel_quantity[4]
    "I3I9f"  # num_wheels, wow[3], gear_pos[3], gear_steer[3], gear_compression[3]
    "Iif"  # cur_time, warp, visibility
    "10f"  # elevator ... spoilers
)
_STRUCT = struct.Struct(_FORMAT)
if _STRUCT.size != PACKET_SIZE:  # pragma: no cover - a layout typo, caught at import
    raise ImportError(f"astra.fgbridge: format packs {_STRUCT.size} bytes, not {PACKET_SIZE}")

# Everything after the accelerations is zero (Q22).
_ZERO_TAIL = (
    (0.0, 0.0)  # stall_warning, slip_deg
    + (0,) * 5  # num_engines, eng_state[4]
    + (0.0,) * 36
    + (0,)  # num_tanks
    + (0.0,) * 4
    + (0,) * 4  # num_wheels, wow[3]
    + (0.0,) * 9
    + (0, 0, 0.0)  # cur_time, warp, visibility
    + (0.0,) * 10
)


class BridgeError(Exception):
    """The bridge refused its source or its settings (module docstring, Refusals)."""


def pack(derived: Mapping[str, float]) -> bytes:
    """One FGNetFDM v24 packet from one frame's derived values (``FIELDS``, SI, no prefix)."""
    d = derived
    flags = _flags(d["status_flags"])
    if flags & STATUS_ACCEL_INVALID:
        accel = (0.0, 0.0, 0.0)
    else:
        accel = (d["ax"] / FOOT, d["ay"] / FOOT, d["az"] / FOOT)
    return _STRUCT.pack(
        FG_NET_FDM_VERSION,
        0,
        d["lon"],
        d["lat"],
        d["altitude"],
        d["altitude"],  # agl: no terrain
        d["phi"],
        d["theta"],
        d["psi"],
        d["alpha"],
        d["beta"],
        0.0,
        0.0,
        0.0,
        d["cas"] / KNOT,
        d["vertical_speed"] / FOOT,
        d["v_n_0"] / FOOT,
        d["v_n_1"] / FOOT,
        d["v_n_2"] / FOOT,
        d["u"] / FOOT,
        d["v"] / FOOT,
        d["w"] / FOOT,
        *accel,
        *_ZERO_TAIL,
    )


class LiveBridge:
    """Sends a live ``Simulation``'s packets while the caller steps it through ``step()`` or
    ``run()`` (module docstring). A context manager that owns its UDP socket."""

    def __init__(
        self,
        sim: _core.Simulation,
        *,
        host: str = DEFAULT_HOST,
        port: int = DEFAULT_PORT,
        divisor: int = DEFAULT_DIVISOR,
        vehicle: int = 0,
        pace: bool = False,
    ) -> None:
        _check_divisor(divisor)
        self._names = _signal_names(vehicle)
        missing = [name for name in self._names.values() if sim.registry.find(name) is None]
        if missing:
            raise BridgeError(
                f"the Simulation has no signal {missing[0]}: a FlightGear packet needs vehicle "
                f"{vehicle}'s derived signals, which only a config with physics publishes"
            )
        self._sim = sim
        self._divisor = divisor
        self._next = sim.clock.frame
        self._sender = _Sender(host, port, pace=pace, dt=sim.clock.dt)
        self.packets_sent = 0

    def step(self) -> _core.HaltReason:
        """Steps the ``Simulation`` one frame. If the frame completed, checks its derived values
        and sends its packet when the frame is due. Then drains the run log, as
        ``astra.lockstep.Session.step`` does: ``Simulation.step()`` never drains, and only
        ``Simulation.run()`` does so on its own (ADR-0025). A frame that completed keeps its
        packet even when the drain after it fails. Returns ``step()``'s ``HaltReason`` if it
        halted, else the drain's (the sticky ``LOG`` halt after a failed write)."""
        current = self._sim.clock.frame
        if current != self._next:
            raise BridgeError(
                f"the Simulation's clock is at frame {current}, but the bridge last left it at "
                f"frame {self._next}: frames stepped outside LiveBridge could not be sent"
            )
        reason = self._sim.step()
        if not reason:
            self._next = self._sim.clock.frame
            self._publish(current)
        drained = self._sim.flush_log()  # no log: does nothing
        return reason if reason else drained

    def _publish(self, frame: int) -> None:
        """Frame ``frame`` completed: the registry holds its derived values (log row ``frame``)."""
        registry = self._sim.registry
        values = {field: cast(float, registry.find(name)) for field, name in self._names.items()}
        _refuse_latlon(values["status_flags"], f"frame {frame} of the live Simulation")
        if frame % self._divisor == 0:
            self._sender.send(frame, pack(values))
            self.packets_sent += 1

    def run(self, frames: int) -> _core.HaltReason | None:
        """Steps up to ``frames`` frames through ``step()``. Returns the halt that stopped the run,
        or ``None``."""
        for _ in range(frames):
            if reason := self.step():
                return reason
        return None

    def close(self) -> None:
        self._sender.close()

    def __enter__(self) -> Self:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        self.close()


def send_log(
    path: str | Path,
    *,
    host: str = DEFAULT_HOST,
    port: int = DEFAULT_PORT,
    divisor: int = DEFAULT_DIVISOR,
    vehicle: int = 0,
    pace: bool = False,
) -> int:
    """Sends the packets of the complete run log at ``path``: one for each logged frame with
    ``frame % divisor == 0``, in frame order (module docstring). Returns the number sent. Every
    refusal, including ``astra.log.load``'s own for an incomplete log, happens before the first
    packet."""
    _check_divisor(divisor)
    names = _signal_names(vehicle)
    run_log = log.load(path)  # strict: an incomplete log is refused, never read
    table, group = _derived_table(run_log, names, path)
    if divisor % group != 0:
        raise BridgeError(
            f"{path}: vehicle {vehicle}'s derived signals are logged every {group} frames, so "
            f"divisor {divisor} would ask for frames the log has no row for"
        )
    frames = np.asarray(table.index.to_numpy(), dtype=np.int64)
    data = np.asarray(table[list(names.values())].to_numpy(), dtype=np.float64)
    flags_column = list(names).index("status_flags")
    for row in range(frames.size):
        _refuse_latlon(float(data[row, flags_column]), f"{path} frame {int(frames[row])}")
    count = 0
    with _Sender(host, port, pace=pace, dt=run_log.dt) as sender:
        for row in np.flatnonzero(frames % divisor == 0):
            values = {field: float(data[row, i]) for i, field in enumerate(names)}
            sender.send(int(frames[row]), pack(values))
            count += 1
    return count


# --- helpers ------------------------------------------------------------------------------------


class _Sender:
    """A UDP socket, with optional wall-clock pacing (Python-only, never sim time)."""

    def __init__(self, host: object, port: object, *, pace: bool, dt: float) -> None:
        if not isinstance(host, str) or not host:
            raise BridgeError(f"host must be a non-empty string, got {host!r}")
        if isinstance(port, bool) or not isinstance(port, int) or not 0 < port < 65536:
            raise BridgeError(f"port must be an integer in [1, 65535], got {port!r}")
        self._address = (host, port)
        self._pace = pace
        self._dt = dt
        self._origin: tuple[float, int] | None = None
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send(self, frame: int, packet: bytes) -> None:
        if self._pace:
            now = time.monotonic()
            if self._origin is None:
                self._origin = (now, frame)
            else:
                due = self._origin[0] + (frame - self._origin[1]) * self._dt
                if due > now:
                    time.sleep(due - now)
        self._socket.sendto(packet, self._address)

    def close(self) -> None:
        self._socket.close()

    def __enter__(self) -> Self:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        self.close()


def _check_divisor(divisor: object) -> None:
    if isinstance(divisor, bool) or not isinstance(divisor, int) or divisor < 1:
        raise BridgeError(f"divisor must be an integer >= 1, got {divisor!r}")


def _signal_names(vehicle: object) -> dict[str, str]:
    if isinstance(vehicle, bool) or not isinstance(vehicle, int) or vehicle < 0:
        raise BridgeError(f"vehicle must be an integer >= 0, got {vehicle!r}")
    return {field: f"v{vehicle}.derived.{field}" for field in FIELDS}


def _flags(value: float) -> int:
    if not float(value).is_integer() or not 0 <= value < 2**32:
        raise BridgeError(f"status_flags {value!r} is not a 32-bit bitmask")
    return int(value)


def _refuse_latlon(status_flags: float, where: str) -> None:
    if _flags(status_flags) & STATUS_LATLON_NOT_MODELED:
        raise BridgeError(
            f"{where}: status_flags carries LATLON_NOT_MODELED (0x2): the Earth model has no "
            "geodetic position (a flat Earth), so the packet's latitude and longitude would be "
            "placeholders (docs/plans/m5-tooling.md R20)"
        )


def _derived_table(run_log: log.RunLog, names: dict[str, str], path: str | Path) -> tuple[Any, int]:
    """The rate-group table holding every derived signal in ``names``, and its divisor."""
    wanted = list(names.values())
    for group, table in sorted(run_log.rates.items()):
        present = [name for name in wanted if name in table.columns]
        if len(present) == len(wanted):
            return table, group
        if present:
            raise BridgeError(f"{path}: the derived signals are split across rate groups")
    missing = next(name for name in wanted if name not in run_log.units)
    raise BridgeError(
        f"{path}: the log has no signal {missing}: a FlightGear packet needs the derived "
        "signals, which only a config with physics publishes"
    )
