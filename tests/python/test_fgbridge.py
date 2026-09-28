"""astra.fgbridge (M5 PR 8; ADR-0026, docs/plans/m5-tooling.md Q21 to Q23, R18 to R20).

The packets travel over a real localhost UDP socket, and every packet is read by a decoder built
from the transcribed oracle table (tests/golden/flightgear/fgnetfdm_v24.py) alone, never from the
packer's own format string. Each mapped field is compared with the run log's row for its frame,
converted to the unit the table states. Two cross-field checks use only decoded values, so a sign
or axis convention that the packer and the test's mapping might share cannot pass unseen:
climb_rate == -v_down, and the NED velocity equals the body velocity rotated by the packet's own
Euler angles.
"""

import dataclasses
import importlib.util
import math
import socket
import struct
from collections.abc import Iterator
from pathlib import Path
from types import ModuleType
from typing import Any

import h5py
import numpy as np
import pytest
import yaml

from astra import _core, fgbridge, log

ROOT = Path(__file__).resolve().parents[2]
CRUISE = ROOT / "aircraft/notional_transport/configs/cruise.yaml"  # wgs84_rotating, Dryden seed 42
FLAT = ROOT / "tests/configs/physics_ballistic_level.yaml"  # earth/flat, with physics
TABLE_PATH = ROOT / "tests/golden/flightgear/fgnetfdm_v24.py"

# A heading, bank and climb off the trim attitude, so every NED axis and the climb rate carry a
# distinct nonzero value (a swapped axis or a flipped sign then changes the packet). Latitude and
# longitude away from zero make lat/lon discriminating.
LAT, LON = 0.6, -2.1  # rad
PSI, PHI, CLIMB = 0.7, 0.2, 0.05  # rad; CLIMB is added to the trim pitch


def _load_table() -> ModuleType:
    spec = importlib.util.spec_from_file_location("fgnetfdm_v24", TABLE_PATH)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


TABLE: Any = _load_table()
_CODES = {"uint32_t": "I", "int32_t": "i", "float": "f", "double": "d"}
_SIZES = {"uint32_t": 4, "int32_t": 4, "float": 4, "double": 8}
_BYTE_ORDER = {"big": ">", "little": "<"}

# What the table's unit strings mean, as a factor from the SI source value (ADR-0002). The test's
# own constants: 1 ft = 0.3048 m, 1 kt = 1852 m per 3600 s, both exact by definition.
_FROM_SI = {
    "rad": 1.0,
    "m": 1.0,
    "kt": 3600.0 / 1852.0,
    "ft/s": 1.0 / 0.3048,
    "ft/s^2": 1.0 / 0.3048,
}

# Q22's mapping: packet field -> log column. v_body comes from the logged state v_b, the
# Earth-relative body velocity (Joe, PR 8 plan review), not from the derived u, v, w.
MAPPED = {
    "longitude": "v0.derived.lon",
    "latitude": "v0.derived.lat",
    "altitude": "v0.derived.altitude",
    "agl": "v0.derived.altitude",
    "phi": "v0.derived.phi",
    "theta": "v0.derived.theta",
    "psi": "v0.derived.psi",
    "alpha": "v0.derived.alpha",
    "beta": "v0.derived.beta",
    "vcas": "v0.derived.cas",
    "climb_rate": "v0.derived.vertical_speed",
    "v_north": "v0.derived.v_n_0",
    "v_east": "v0.derived.v_n_1",
    "v_down": "v0.derived.v_n_2",
    "v_body_u": "v0.state.v_b_0",
    "v_body_v": "v0.state.v_b_1",
    "v_body_w": "v0.state.v_b_2",
    "A_X_pilot": "v0.derived.ax",
    "A_Y_pilot": "v0.derived.ay",
    "A_Z_pilot": "v0.derived.az",
}
ACCELS = ("A_X_pilot", "A_Y_pilot", "A_Z_pilot")


# --- the decoder: the golden table only -----------------------------------------------------------


def _decoder() -> tuple[struct.Struct, list[tuple[str, int]]]:
    fmt = _BYTE_ORDER[TABLE.BYTE_ORDER]
    position = 0
    layout: list[tuple[str, int]] = []
    for field in TABLE.FIELDS:
        assert field.offset >= position, field.name
        fmt += "x" * (field.offset - position)  # padding the table records, if any
        fmt += f"{field.length}{_CODES[field.ctype]}"
        position = field.offset + _SIZES[field.ctype] * field.length
        layout.append((field.name, field.length))
    fmt += "x" * (TABLE.SIZE - position)
    decoder = struct.Struct(fmt)
    assert decoder.size == TABLE.SIZE
    return decoder, layout


DECODER, LAYOUT = _decoder()


def decode(packet: bytes) -> dict[str, Any]:
    assert len(packet) == TABLE.SIZE
    flat = DECODER.unpack(packet)
    out: dict[str, Any] = {}
    i = 0
    for name, length in LAYOUT:
        out[name] = flat[i] if length == 1 else tuple(flat[i : i + length])
        i += length
    return out


def _ulp32(x: float) -> float:
    return float(np.spacing(np.float32(abs(x))))


# --- fixtures and helpers -------------------------------------------------------------------------


@pytest.fixture
def receiver() -> Iterator[socket.socket]:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(5.0)
    yield sock
    sock.close()


def _port(sock: socket.socket) -> int:
    return int(sock.getsockname()[1])


def _receive(sock: socket.socket, count: int) -> list[bytes]:
    """Exactly ``count`` datagrams, then proof that no further one is waiting."""
    packets = [sock.recv(65536) for _ in range(count)]
    sock.setblocking(False)
    try:
        with pytest.raises(BlockingIOError):
            sock.recv(65536)
    finally:
        sock.settimeout(5.0)
    return packets


def _nothing_received(sock: socket.socket) -> None:
    _receive(sock, 0)


def _cruise(frames: int) -> dict[str, Any]:
    d = yaml.safe_load(CRUISE.read_text())
    d["sim"]["frames"] = frames
    state = d["initial"]["state"]
    state["p_n"][0], state["p_n"][1] = LAT, LON
    theta = state["euler_nb"][1]
    state["euler_nb"] = [PSI, theta + CLIMB, PHI]
    return d


def _sim(d: dict[str, Any], log_path: Path | None, **log_settings: Any) -> _core.Simulation:
    data = dict(d) if log_path is None else {**d, "log": {"path": str(log_path), **log_settings}}
    return _core.Simulation.from_config(_core.Config.from_dict(data), _core.default_plugin_dir())


def _live_run(
    d: dict[str, Any], log_path: Path, receiver: socket.socket, frames: int, divisor: int
) -> tuple[list[bytes], log.RunLog]:
    """Runs ``d`` for ``frames`` through a LiveBridge, draining the socket after every step, and
    returns the packets and the run's own log."""
    sim = _sim(d, log_path)
    packets: list[bytes] = []
    with fgbridge.LiveBridge(sim, port=_port(receiver), divisor=divisor) as bridge:
        for _ in range(frames):
            before = bridge.packets_sent
            assert not bridge.step()
            packets += _receive(receiver, bridge.packets_sent - before)
    sim.close_log()
    return packets, log.load(log_path)


def _expected_frames(frames: int, divisor: int) -> list[int]:
    return [k for k in range(frames) if k % divisor == 0]


def _check_packet(packet: bytes, row: Any) -> dict[str, Any]:
    """R18 for one packet against the log row of its frame. Returns the decoded packet."""
    p = decode(packet)
    assert len(packet) == 408
    assert p["version"] == 24 == TABLE.FG_NET_FDM_VERSION
    assert p["padding"] == 0
    flags = int(row["v0.derived.status_flags"])
    by_name = {field.name: field for field in TABLE.FIELDS}
    for name, column in MAPPED.items():
        field = by_name[name]
        expected = float(row[column]) * _FROM_SI[field.unit]
        if name in ACCELS and flags & fgbridge.STATUS_ACCEL_INVALID:
            expected = 0.0
        if field.ctype == "double":
            assert p[name] == expected, name  # rad and m: no conversion, exact
        else:
            assert abs(p[name] - expected) <= _ulp32(expected), (name, p[name], expected)
    for field in TABLE.FIELDS:
        if field.name in MAPPED or field.name == "version":
            continue
        value = p[field.name]
        assert (value == 0) if field.length == 1 else all(v == 0 for v in value), field.name
    _check_cross_fields(p)
    return p


def _check_cross_fields(p: dict[str, Any]) -> None:
    """Relations among decoded values alone, independent of the mapping (Joe, PR 8 plan review)."""
    climb, down = p["climb_rate"], p["v_down"]
    assert abs(climb + down) <= _ulp32(down), (climb, down)
    phi, theta, psi = p["phi"], p["theta"], p["psi"]
    cf, sf = math.cos(phi), math.sin(phi)
    ct, st = math.cos(theta), math.sin(theta)
    cp, sp = math.cos(psi), math.sin(psi)
    # C_nb^T: body to NED for 3-2-1 Euler angles (psi about z, theta about y, phi about x).
    body_to_ned = np.array(
        [
            [ct * cp, sf * st * cp - cf * sp, cf * st * cp + sf * sp],
            [ct * sp, sf * st * sp + cf * cp, cf * st * sp - sf * cp],
            [-st, sf * ct, cf * ct],
        ]
    )
    v_body = np.array([p["v_body_u"], p["v_body_v"], p["v_body_w"]])
    v_ned = np.array([p["v_north"], p["v_east"], p["v_down"]])
    # Float32 rounding of three angles and six velocities: a few float32 epsilons of |v|.
    tolerance = 8 * float(np.finfo(np.float32).eps) * float(np.linalg.norm(v_body))
    assert np.max(np.abs(body_to_ned @ v_body - v_ned)) <= tolerance, (body_to_ned @ v_body, v_ned)


def _rows(run_log: log.RunLog) -> Any:
    return run_log.rates[1]


# --- R18, R19 and the carry-overs -----------------------------------------------------------------


def test_the_decoder_is_the_table_and_the_packet_size_is_408() -> None:
    assert TABLE.SIZE == fgbridge.PACKET_SIZE == 408
    assert DECODER.size == 408
    ends = [f.offset + _SIZES[f.ctype] * f.length for f in TABLE.FIELDS]
    assert [f.offset for f in TABLE.FIELDS[1:]] == ends[:-1]  # contiguous, as g++ lays it out
    assert ends[-1] == TABLE.SIZE


@pytest.mark.parametrize(("frames", "divisor"), [(50, 1), (200, 7), (400, 16)])
def test_live_and_log_sources_send_the_logged_frames_values_byte_identically(
    tmp_path: Path, receiver: socket.socket, frames: int, divisor: int
) -> None:
    live, run_log = _live_run(_cruise(frames), tmp_path / "run.h5", receiver, frames, divisor)
    rows = _rows(run_log)
    expected = _expected_frames(frames, divisor)
    assert len(live) == len(expected)
    for packet, frame in zip(live, expected, strict=True):  # R18, R19: packet i is frame i * d
        _check_packet(packet, rows.loc[frame])
    sent = fgbridge.send_log(tmp_path / "run.h5", port=_port(receiver), divisor=divisor)
    assert sent == len(expected)
    assert _receive(receiver, sent) == live  # one definition of frame k's values


def test_default_divisor_is_16(tmp_path: Path, receiver: socket.socket) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(_cruise(100), path)
    with fgbridge.LiveBridge(sim, port=_port(receiver)) as bridge:
        assert bridge.run(100) is None
    sim.close_log()
    assert bridge.packets_sent == 7  # frames 0, 16, ..., 96
    live = _receive(receiver, 7)
    assert fgbridge.send_log(path, port=_port(receiver)) == 7
    assert _receive(receiver, 7) == live


def test_frame_zero_packs_zero_acceleration_while_the_icd_marks_it_invalid(
    tmp_path: Path, receiver: socket.socket
) -> None:
    live, run_log = _live_run(_cruise(20), tmp_path / "run.h5", receiver, 20, 16)
    rows = _rows(run_log)
    assert int(rows.loc[0, "v0.derived.status_flags"]) & fgbridge.STATUS_ACCEL_INVALID
    assert not int(rows.loc[16, "v0.derived.status_flags"]) & fgbridge.STATUS_ACCEL_INVALID
    first, later = _check_packet(live[0], rows.loc[0]), _check_packet(live[1], rows.loc[16])
    assert [first[a] for a in ACCELS] == [0.0, 0.0, 0.0]
    assert later["A_Z_pilot"] < -20.0  # about -g in ft/s^2: specific force, gravity excluded
    assert all(later[a] != 0.0 for a in ("A_X_pilot", "A_Z_pilot"))


def test_the_packer_zeroes_acceleration_whenever_the_invalid_bit_is_set() -> None:
    values: dict[str, float] = dict.fromkeys(fgbridge.FIELDS, 1.0)
    values["status_flags"] = float(fgbridge.STATUS_ACCEL_INVALID)
    p = decode(fgbridge.pack(values))
    assert [p[a] for a in ACCELS] == [0.0, 0.0, 0.0]
    values["status_flags"] = 0.0
    p = decode(fgbridge.pack(values))
    assert all(p[a] == pytest.approx(1.0 / 0.3048) for a in ACCELS)


def test_v_body_is_the_earth_relative_velocity_not_the_air_relative_one(
    tmp_path: Path, receiver: socket.socket
) -> None:
    live, run_log = _live_run(_cruise(64), tmp_path / "run.h5", receiver, 64, 16)
    rows = _rows(run_log)
    for packet, frame in zip(live, _expected_frames(64, 16), strict=True):
        row = rows.loc[frame]
        wind = [float(row[f"wind.{axis}"]) for axis in ("north", "east", "down")]
        assert max(abs(w) for w in wind) > 1.0  # Dryden on: air and Earth velocities differ
        tas, alpha, beta = (float(row[f"v0.derived.{n}"]) for n in ("tas", "alpha", "beta"))
        # The ICD's air-relative body velocity, rebuilt from its own alpha and beta definitions.
        air = [
            tas * math.cos(beta) * math.cos(alpha),
            tas * math.sin(beta),
            tas * math.cos(beta) * math.sin(alpha),
        ]
        earth = [float(row[f"v0.state.v_b_{i}"]) for i in range(3)]
        p = _check_packet(packet, row)
        got = [p["v_body_u"], p["v_body_v"], p["v_body_w"]]
        for g, e in zip(got, earth, strict=True):
            assert abs(g - e / 0.3048) <= _ulp32(e / 0.3048)
        assert max(abs(g - a / 0.3048) for g, a in zip(got, air, strict=True)) > 1.0
        derived = [float(row[f"v0.derived.{n}"]) for n in ("u", "v", "w")]
        assert derived == earth  # the ICD packs v_b itself


def test_the_log_source_loads_strictly(
    tmp_path: Path, receiver: socket.socket, monkeypatch: pytest.MonkeyPatch
) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(_cruise(40), path)
    assert not sim.run(40)
    sim.close_log()
    calls: list[dict[str, Any]] = []
    real_load = log.load

    def spy(p: str | Path, **kwargs: Any) -> log.RunLog:
        calls.append(kwargs)
        return real_load(p, **kwargs)

    monkeypatch.setattr(fgbridge.log, "load", spy)
    assert fgbridge.send_log(path, port=_port(receiver)) == 3
    assert calls == [{}]  # never allow_incomplete
    _receive(receiver, 3)
    tampered = tmp_path / "tampered.h5"
    tampered.write_bytes(path.read_bytes())
    with h5py.File(tampered, "r+") as f:
        f.attrs["complete"] = 0
    with pytest.raises(log.IncompleteLogError):
        fgbridge.send_log(tampered, port=_port(receiver))
    _nothing_received(receiver)


# --- R20 and the other refusals -------------------------------------------------------------------


def _flat(frames: int) -> dict[str, Any]:
    d = yaml.safe_load(FLAT.read_text())
    d["sim"]["frames"] = frames
    return d


def test_a_flat_earth_live_run_is_refused_at_its_first_frame(receiver: socket.socket) -> None:
    sim = _sim(_flat(10), None)
    with (
        fgbridge.LiveBridge(sim, port=_port(receiver), divisor=1) as bridge,
        pytest.raises(fgbridge.BridgeError, match=r"LATLON_NOT_MODELED \(0x2\)"),
    ):
        bridge.step()
    assert bridge.packets_sent == 0
    _nothing_received(receiver)


def test_a_flat_earth_log_is_refused_before_any_packet(
    tmp_path: Path, receiver: socket.socket
) -> None:
    path = tmp_path / "flat.h5"
    sim = _sim(_flat(10), path)
    assert not sim.run(10)
    sim.close_log()
    with pytest.raises(fgbridge.BridgeError, match=r"frame 0: .*LATLON_NOT_MODELED \(0x2\)"):
        fgbridge.send_log(path, port=_port(receiver), divisor=1)
    _nothing_received(receiver)


@pytest.mark.parametrize("divisor", [0, -1, 1.5, True, "16"])
def test_a_divisor_that_is_not_a_positive_integer_is_refused(tmp_path: Path, divisor: Any) -> None:
    sim = _sim(_cruise(10), None)
    with pytest.raises(fgbridge.BridgeError, match="divisor"):
        fgbridge.LiveBridge(sim, divisor=divisor)
    with pytest.raises(fgbridge.BridgeError, match="divisor"):
        fgbridge.send_log(tmp_path / "absent.h5", divisor=divisor)


@pytest.mark.parametrize(
    ("host", "port"), [("", 5500), ("127.0.0.1", 0), ("127.0.0.1", 65536), ("127.0.0.1", True)]
)
def test_a_bad_address_is_refused(host: Any, port: Any) -> None:
    sim = _sim(_cruise(10), None)
    with pytest.raises(fgbridge.BridgeError, match=r"host|port"):
        fgbridge.LiveBridge(sim, host=host, port=port)


def test_frames_stepped_outside_the_live_bridge_are_refused(receiver: socket.socket) -> None:
    sim = _sim(_cruise(10), None)
    with fgbridge.LiveBridge(sim, port=_port(receiver), divisor=1) as bridge:
        assert not bridge.step()
        assert not sim.step()  # frame 1, behind the bridge's back
        with pytest.raises(fgbridge.BridgeError, match=r"clock is at frame 2.*frame 1"):
            bridge.step()
    assert _receive(receiver, 1)  # frame 0 only


def test_a_source_without_derived_signals_is_refused(tmp_path: Path) -> None:
    no_physics = {"sim": {"dt": 0.001, "frames": 5}, "vehicle": {"models": []}}
    with pytest.raises(fgbridge.BridgeError, match=r"no signal v0\.derived\.lon"):
        fgbridge.LiveBridge(_sim(no_physics, None))
    path = tmp_path / "none.h5"
    sim = _sim(no_physics, path)
    assert not sim.run(5)
    sim.close_log()
    with pytest.raises(fgbridge.BridgeError, match=r"no signal v0\.derived\.lon"):
        fgbridge.send_log(path)
    with pytest.raises(fgbridge.BridgeError, match=r"no signal v1\.derived\.lon"):
        fgbridge.LiveBridge(_sim(_cruise(5), None), vehicle=1)  # ADR-0011: v1 does not exist


def test_a_divisor_the_logged_rate_cannot_serve_is_refused(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, receiver: socket.socket
) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(_cruise(20), path)
    assert not sim.run(20)
    sim.close_log()
    real = log.load(path)
    # A log whose derived signals sit in a divisor-2 group (none does today: every signal is
    # registered at divisor 1).
    relabeled = dataclasses.replace(real, rates={2: real.rates[1]})

    def load(path: str | Path) -> log.RunLog:
        del path
        return relabeled

    monkeypatch.setattr(fgbridge.log, "load", load)
    with pytest.raises(fgbridge.BridgeError, match="logged every 2 frames, so divisor 3"):
        fgbridge.send_log(path, port=_port(receiver), divisor=3)
    _nothing_received(receiver)


def test_the_live_bridge_drains_the_log_past_its_ring_capacity(
    tmp_path: Path, receiver: socket.socket
) -> None:
    """R-001: Simulation.step() never drains, so without the bridge's own drain an 8-row ring
    halts LOG at frame 8. With it, 100 frames complete and every packet matches the log's."""
    path = tmp_path / "run.h5"
    sim = _sim(_cruise(100), path, buffer_frames=8)
    packets: list[bytes] = []
    with fgbridge.LiveBridge(sim, port=_port(receiver), divisor=3) as bridge:
        for _ in range(100):
            before = bridge.packets_sent
            assert not bridge.step()
            packets += _receive(receiver, bridge.packets_sent - before)
    sim.close_log()
    assert sim.clock.frame == 100
    assert len(packets) == len(_expected_frames(100, 3)) == 34
    assert fgbridge.send_log(path, port=_port(receiver), divisor=3) == 34
    assert _receive(receiver, 34) == packets
    path2 = tmp_path / "run2.h5"  # run() goes through step(), so it drains too
    sim2 = _sim(_cruise(100), path2, buffer_frames=8)
    with fgbridge.LiveBridge(sim2, port=_port(receiver), divisor=50) as bridge2:
        assert bridge2.run(100) is None
    sim2.close_log()
    _receive(receiver, 2)


class _FailingDrain:
    """A Simulation whose flush_log() reports ``halt``, as a failed drain write would; everything
    else is the real Simulation's."""

    def __init__(self, sim: _core.Simulation, halt: _core.HaltReason) -> None:
        self._sim = sim
        self._halt = halt

    def flush_log(self) -> _core.HaltReason:
        return self._halt

    def __getattr__(self, name: str) -> Any:
        return getattr(self._sim, name)


def test_a_failed_drain_is_returned_after_the_completed_frames_packet(
    receiver: socket.socket,
) -> None:
    donor = _sim(_cruise(10), None)
    with pytest.raises(_core.Error):
        donor.write_input("controls.throttle", math.nan)
    halt = donor.step()  # a real, truthy HaltReason to stand in for the drain's LOG halt
    assert halt
    proxy: Any = _FailingDrain(_sim(_cruise(10), None), halt)
    with fgbridge.LiveBridge(proxy, port=_port(receiver), divisor=1) as bridge:
        assert bridge.step() is halt  # frame 0 completed, then the drain failed
    assert bridge.packets_sent == 1
    assert proxy.clock.frame == 1
    _receive(receiver, 1)


def test_a_halted_step_sends_nothing(receiver: socket.socket) -> None:
    sim = _sim(_cruise(10), None)
    with pytest.raises(_core.Error):
        sim.write_input("controls.throttle", math.nan)  # pending: frame 0's latch halts on it
    with fgbridge.LiveBridge(sim, port=_port(receiver), divisor=1) as bridge:
        reason = bridge.step()
    assert reason
    assert reason.check == _core.HealthCheck.NON_FINITE_INPUT
    assert bridge.packets_sent == 0
    _nothing_received(receiver)


# --- pacing (Python-only, off by default; no real wait here) --------------------------------------


def test_pacing_holds_each_packet_until_its_frame_time(
    tmp_path: Path, receiver: socket.socket, monkeypatch: pytest.MonkeyPatch
) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(_cruise(50), path)
    assert not sim.run(50)
    sim.close_log()
    sleeps: list[float] = []

    def frozen() -> float:  # wall time stands still, so every later packet waits its full offset
        return 100.0

    monkeypatch.setattr(fgbridge.time, "monotonic", frozen)
    monkeypatch.setattr(fgbridge.time, "sleep", sleeps.append)
    assert fgbridge.send_log(path, port=_port(receiver), divisor=16, pace=True) == 4
    _receive(receiver, 4)
    assert sleeps == pytest.approx([0.016, 0.032, 0.048], rel=1e-12)
    sleeps.clear()
    assert fgbridge.send_log(path, port=_port(receiver), divisor=16, pace=False) == 4
    _receive(receiver, 4)
    assert sleeps == []
