"""M7b's oracle (a): the exact sampled-data closed-loop check (docs/plans/m7b-pr2-sampled-data.md).

A linear plant with model-owned state (tests/plugins/linear_plant, ADR-0021) runs under discrete
state feedback (tests/plugins/state_feedback_peer) through the exchange stage (ADR-0022). Its
trajectory is checked against the exact zero-order-hold solution from sampled_data_reference.py,
which shares no code with the path under test (ADR-0034 Decision 2):
- F, per frame: x(j+1) against Phi_1 x(j) + Gamma_1 u_hold(j), within a running roundoff bound;
- L, per exchange: the recorded reply against G r - K x;
- C, per reference exchange: x(kN) against the exact closed-loop recursion;
- the request sequence, bitwise.
Every bound is the plan's §7 and every registration its §9, both fixed before the first run
(ADR-0034 Decision 3). Every pre-registered constant below cites its plan section (amendment 8,
ADR-0034 Decision 6); the reviewer compares each with the plan's.

The tests named test_sampled_data_reference_* run the reference alone. Every other test runs the
plant and the peer.
"""

import ast
import math
import re
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path

import pytest

import sampled_data_reference as ref
from astra import _core
from astra.lockstep import ICD_MAGIC, ICD_VERSION, parse_recording

# §6: the run.
FRAMES = 2001
DT_TEXT = "0.001"
STEP_FRAME = 1007  # r steps here; it acts from the first exchange at or after it
REFERENCE_TEXT = "1.0"  # r after the step; 0.0 before it
X0_NOMINAL = ("0.0", "0.0", "0.0", "1.0")  # e_4, the nominal runs and the four controls
X0_ORDER = ("0.0", "0.0", "0.0", "0.0", "0.0")  # the order control
A_TEXT = "100.0"  # 1/tau on the chain's superdiagonal, A = S / tau
B_TEXT = "100.0"  # B = e_n / tau
ELEVATOR_LIMITS = ("-1000.0", "1000.0")  # §6's limits

# §6: the gains, as the design's rationals (h = 1/1000, a = b = 100 exactly) and as the config text
# that is the nearest double to each. G is K_1, as the same text.
GAINS: dict[int, tuple[tuple[str, ...], tuple[str, ...]]] = {
    10: (
        ("1/16", "13/32", "203/192", "269/192"),
        ("0.0625", "0.40625", "1.0572916666666667", "1.4010416666666667"),
    ),
    3: (
        ("625/81", "1625/108", "5075/432", "1345/288"),
        ("7.716049382716049", "15.046296296296296", "11.747685185185185", "4.670138888888889"),
    ),
    1: (
        ("625", "1625/4", "5075/48", "1345/96"),
        ("625.0", "406.25", "105.72916666666667", "14.010416666666666"),
    ),
}
ORDER_GAINS = (
    ("1/32", "1/4", "107/128", "145/96", "1531/960"),
    ("0.03125", "0.25", "0.8359375", "1.5104166666666667", "1.5947916666666666"),
)
ORDER_DIVISOR = 10  # §9: the order control runs at N = 10

# §7: C's reference grid, I's expected values, S-bar's upward rounding (§16, P-02), and
# amendment 6's gap.
GRID_BITS = 400
S_BAR_ROUND_BITS = 128
EXPECTED_I = {10: 12, 3: 10, 1: 18}
C6_GAP = Fraction(-1, 3 * 2**65)  # c6 - h/6

# §9: the registrations. A margin is residual over bound.
FIRST_FAILURE_MARGIN = 10**10
WINDOW_MARGIN = 10**3
ORDER_MARGIN = 10**8
CONTROL_DIVISOR = 10  # every control's reference N, and the plant controls' divisor
WRONG_DIVISOR = 11  # both divisor keys of the wrong-divisor control
WRONG_EXCHANGES = 182  # §9 (P-01): its recorded exchanges, against the reference's 201
LONG_HOLD_FRAME = 1010
LATE_F_WINDOWS = ((0, 40), (101, 135))  # exchange indices k: F fails at frame k N
LATE_C_FIRST = 1
LATE_C_WINDOWS = ((1, 50), (102, 150))
LONG_C_FIRST = 102
LONG_C_WINDOWS = ((102, 135),)
WRONG_C_FIRST = 2
WRONG_C_WINDOWS = ((2, 45), (102, 145))
STAGE_F_WINDOWS = ((0, 30), (101, 125))
STAGE_C_FIRST = 1
STAGE_C_WINDOWS = ((1, 45), (102, 145))
ORDER_FIRST_FRAME = 1010
ORDER_FRAMES = (1010, 1029)  # the excited frames, F on x_1


def _list(values: tuple[str, ...] | list[str]) -> str:
    return "[" + ", ".join(values) + "]"


def config_text(
    *,
    n: int = 4,
    divisor: int = 10,
    peer_divisor: int | None = None,
    gains: tuple[str, ...] = GAINS[10][1],
    x0: tuple[str, ...] = X0_NOMINAL,
    plant_lines: tuple[str, ...] = (),
    peer_dt: str = DT_TEXT,
    peer_state: str = "v0.linear_plant.state",
    schedule: str = f"[{{ frame: {STEP_FRAME}, reference: {REFERENCE_TEXT} }}]",
) -> str:
    """The plan's §2 config, for a chain of n states at divisor N."""
    rows = [_list([A_TEXT if j == i + 1 else "0.0" for j in range(n)]) for i in range(n)]
    b_rows = [_list([B_TEXT if i == n - 1 else "0.0"]) for i in range(n)]
    plant_extra = "".join(f"      {line}\n" for line in plant_lines)
    return (
        f"sim: {{ dt: {DT_TEXT}, frames: {FRAMES} }}\n"
        "vehicle:\n"
        "  models:\n"
        "    - plugin: tests/plugins/linear_plant\n"
        f"      a: [{', '.join(rows)}]\n"
        f"      b: [{', '.join(b_rows)}]\n"
        f"      x0: {_list(x0)}\n"
        "      inputs: [plant.u]\n"
        f"{plant_extra}"
        "  initial_controls: { plant: { u: 0.0 } }\n"
        "physical:\n"
        "  earth: plugins/physical/earth/flat\n"
        "  gravity: plugins/physical/gravity/constant\n"
        "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
        "izz: 1.0 }\n"
        "initial:\n"
        "  state: { p_n: [0.0, 0.0, -100.0], v_b: [60.0, 0.0, 0.0] }\n"
        "transport:\n"
        "  plugin: tests/plugins/state_feedback_peer\n"
        f"  divisor: {divisor}\n"
        "  commands: { elevator: plant.u }\n"
        f"  limits: {{ elevator: {_list(ELEVATOR_LIMITS)}, aileron: [-1.0, 1.0], "
        "rudder: [-1.0, 1.0], "
        "throttle: [0.0, 1.0] }\n"
        "  peer:\n"
        f"    state: {peer_state}\n"
        f"    divisor: {divisor if peer_divisor is None else peer_divisor}\n"
        f"    dt: {peer_dt}\n"
        f"    k: {_list(gains)}\n"
        f"    g: {gains[0]}\n"
        "    reference: 0.0\n"
        f"    schedule: {schedule}\n"
    )


def nominal_text(divisor: int) -> str:
    return config_text(divisor=divisor, gains=GAINS[divisor][1])


def order_text() -> str:
    return config_text(n=5, divisor=ORDER_DIVISOR, gains=ORDER_GAINS[1], x0=X0_ORDER)


# --- Running the path under test ---


@dataclass(frozen=True)
class Run:
    states: list[list[float]]  # x(0) .. x(FRAMES), the plant's slice after each step
    requests: list[tuple[int, float, int, int]]  # frame, t, magic, version
    replies: list[tuple[int, float]]  # echoed frame, elevator
    halt: _core.HaltReason | None
    signals: dict[str, float]
    dt: float
    recorder: bytes


def simulate(text: str) -> Run:
    sim = _core.Simulation.from_config(
        _core.Config.from_string(text, "test_sampled_data"), _core.default_plugin_dir()
    )
    vehicle = sim.vehicles[0]
    offset, count = next(
        (o, c) for name, o, c in vehicle.state_owner_slices if name == "linear_plant"
    )
    states: list[list[float]] = [vehicle.state[offset : offset + count].tolist()]
    halt = None
    for _ in range(FRAMES):
        result = sim.step()
        if result:
            halt = result
            break
        states.append(vehicle.state[offset : offset + count].tolist())
    recorder = sim.exchange_recorder_bytes()
    assert recorder is not None
    recording = parse_recording(recorder)
    requests = [
        (int(r["frame"]), float(r["t"]), int(r["magic"]), int(r["version"]))
        for r in recording.requests
    ]
    replies = [(int(r["frame"]), float(r["elevator"])) for r in recording.replies]
    signals = {name: value for name, value, _unit, _divisor in sim.registry.signals}
    return Run(states, requests, replies, halt, signals, sim.clock.dt, recorder)


# --- Applying the reference ---


@dataclass(frozen=True)
class Checks:
    f_residual: list[ref.Vector]  # per frame j
    f_bound: list[ref.Vector]
    l_residual: list[Fraction]  # per recorded exchange
    l_bound: list[Fraction]
    c_residual: list[ref.Vector]  # per reference exchange k
    c_bound: list[ref.Vector]
    request_ok: list[bool]  # per recorded exchange
    sequence_ok: bool  # §16 P-01: the full expected sequence, the last due exchange included


def _bits(value: float) -> str:
    """A double's IEEE representation (§16 P-03): -0.0 and +0.0 differ."""
    return value.hex()


def evaluate(p: ref.Params, run: Run) -> Checks:
    """F, L, C and the request checks of run against the reference p (the plan's §7)."""
    n = p.n
    frames = len(run.states) - 1
    phi1, gam1 = ref.zoh(p, 1)
    holds = [Fraction(u) for _, u in run.replies]
    exchange_frames = [frame for frame, _, _, _ in run.requests]
    states = [[Fraction(v) for v in x] for x in run.states]

    f_residual: list[ref.Vector] = []
    f_bound: list[ref.Vector] = []
    index = -1
    for j in range(frames):
        while index + 1 < len(exchange_frames) and exchange_frames[index + 1] <= j:
            index += 1
        u = holds[index]
        target = [v + g * u for v, g in zip(ref.matvec(phi1, states[j]), gam1, strict=True)]
        f_residual.append([abs(states[j + 1][i] - target[i]) for i in range(n)])
        f_bound.append(ref.f_bound(p, states[j], u))

    l_residual: list[Fraction] = []
    l_bound: list[Fraction] = []
    for frame, u in zip(exchange_frames, holds, strict=True):
        x = states[frame]
        r = p.reference_at(frame)
        l_residual.append(abs(u - ref.law(p, x, r)))
        l_bound.append(ref.l_bound(p, x, r))

    big_n = p.peer_divisor
    count = frames // big_n + 1  # every k with k N <= FRAMES
    _, _, sbar = ref.s_bar(p, S_BAR_ROUND_BITS)  # §7: C uses the rounded S-bar (P-02)
    reference = ref.reference_states(p, count, GRID_BITS)
    c_bound = ref.c_bounds(p, count, f_bound, l_bound, sbar, GRID_BITS)
    c_residual = [
        [abs(states[k * big_n][i] - reference[k][i]) for i in range(n)] for k in range(count)
    ]

    h = float(p.h)
    request_ok = []
    for k, ((frame, t, magic, version), (echo, _)) in enumerate(
        zip(run.requests, run.replies, strict=True)
    ):
        expected = k * big_n
        request_ok.append(
            frame == expected
            and _bits(t) == _bits(float(expected) * h)
            and magic == ICD_MAGIC
            and version == ICD_VERSION
            and echo == frame
        )
    # §7's full sequence (P-01): ceil(2001/N) requests and replies, at frames 0, N, 2N, ... up to
    # the last multiple of N at or below 2000, each reply echoing its request's frame.
    due = math.ceil(FRAMES / big_n)
    sequence_ok = (
        len(run.requests) == due
        and len(run.replies) == due
        and exchange_frames == [k * big_n for k in range(due)]
        and all(
            echo == frame
            for (frame, _, _, _), (echo, _) in zip(run.requests, run.replies, strict=True)
        )
    )
    return Checks(
        f_residual, f_bound, l_residual, l_bound, c_residual, c_bound, request_ok, sequence_ok
    )


def _ratio(residual: Fraction, bound: Fraction) -> Fraction:
    return residual / bound if bound > 0 else Fraction(10**400) if residual > 0 else Fraction(0)


def f_ratio(checks: Checks, j: int, components: range | None = None) -> Fraction:
    comps = components if components is not None else range(len(checks.f_bound[j]))
    return max(_ratio(checks.f_residual[j][i], checks.f_bound[j][i]) for i in comps)


def c_ratio(checks: Checks, k: int) -> Fraction:
    return max(_ratio(r, b) for r, b in zip(checks.c_residual[k], checks.c_bound[k], strict=True))


def l_ratio(checks: Checks, k: int) -> Fraction:
    return _ratio(checks.l_residual[k], checks.l_bound[k])


def _window(windows: tuple[tuple[int, int], ...]) -> list[int]:
    return [k for lo, hi in windows for k in range(lo, hi + 1)]


def _log10(value: Fraction) -> float:
    if value == 0:
        return float("-inf")
    return math.log10(value.numerator) - math.log10(value.denominator)


def assert_completed(run: Run) -> None:
    if run.halt is not None:
        pytest.fail(f"halted: {run.halt.check} at frame {run.halt.frame}")
    assert len(run.states) == FRAMES + 1
    assert run.signals["transport.peer.refused"] == 0.0


# --- The reference alone (no run of the plant or the peer) ---


def test_sampled_data_reference_imports_only_fractions_math_and_yaml() -> None:
    """§3: the reference imports nothing from astra and links nothing (ADR-0034 Decision 2)."""
    tree = ast.parse(Path(ref.__file__).read_text())
    imported: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            imported.update(alias.name.split(".")[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom):
            assert node.module is not None
            imported.add(node.module.split(".")[0])
    assert imported == {"fractions", "math", "yaml"}


@pytest.mark.parametrize("divisor", [10, 3, 1])
def test_sampled_data_reference_gains_place_the_design_poles_at_one_half(divisor: int) -> None:
    """§6: the config text is each rational's nearest double; the design's poles are exactly 1/2."""
    rationals, texts = GAINS[divisor]
    _check_gains(nominal_text(divisor), rationals, texts, n=4)


def test_sampled_data_reference_order_gains_place_the_design_poles_at_one_half() -> None:
    _check_gains(order_text(), *ORDER_GAINS, n=5)


def _check_gains(text: str, rationals: tuple[str, ...], texts: tuple[str, ...], n: int) -> None:
    for rational, value in zip(rationals, texts, strict=True):
        assert repr(float(Fraction(rational))) == value
    design = ref.Params(text)
    design.h = Fraction(1, 1000)  # the design's h, exactly
    design.k = [Fraction(r) for r in rationals]
    design.g = design.k[0]
    phi_cl, _ = ref.closed_loop(design)
    expected = [math.comb(n, i) * Fraction(-1, 2) ** i for i in range(n + 1)]
    assert ref.characteristic_polynomial(phi_cl) == expected


def test_sampled_data_reference_rk4_is_exact_for_four_states_and_misses_the_fifth() -> None:
    """§7's premise and R1: T4(hM) = e^(hM) iff M^5 = 0; at n = 5 RK4 omits h^5 A^4 B/120 on x_1."""
    four = ref.Params(nominal_text(10))
    tx, tu = ref.rk4_tableau(four)
    phi1, gam1 = ref.zoh(four, 1)
    assert tx == phi1
    assert tu == gam1
    five = ref.Params(order_text())
    tx, tu = ref.rk4_tableau(five)
    phi1, gam1 = ref.zoh(five, 1)
    a, b = five.chain()
    omitted = five.h**5 * a**4 * b / 120
    assert tx == phi1
    assert tu == [gam1[0] - omitted, *gam1[1:]]


def test_sampled_data_reference_c6_gap_is_the_plans_value() -> None:
    """§7, amendment 6: c6 - h/6 = -1/(3 2^65) for h = the double 0.001."""
    p = ref.Params(nominal_text(10))
    assert ref.c6(p) - p.h / 6 == C6_GAP


@pytest.mark.parametrize("divisor", [10, 3, 1])
def test_sampled_data_reference_s_bar_index_is_the_plans_value(divisor: int) -> None:
    """§7: I, the smallest m with ||Phi_cl^m||_inf <= 1/2; S-bar rounded >= S-bar exact (P-02)."""
    i, exact, rounded = ref.s_bar(ref.Params(nominal_text(divisor)), S_BAR_ROUND_BITS)
    assert i == EXPECTED_I[divisor]
    assert all(v >= 0 for row in exact for v in row)
    assert all(
        r >= e
        for row_r, row_e in zip(rounded, exact, strict=True)
        for r, e in zip(row_r, row_e, strict=True)
    )


# --- The nominal runs (R2) ---


@pytest.mark.parametrize("divisor", [10, 3, 1])
def test_sampled_data_nominal_run_matches_the_exact_solution(divisor: int) -> None:
    """R2: F, L and C within their bounds at every frame and exchange; the request sequence; parse
    identity; completion."""
    text = nominal_text(divisor)
    p = ref.Params(text)
    assert p.chain() == (Fraction(100), Fraction(100))  # the n = 4 chain: RK4 is exact here
    assert p.n == 4
    run = simulate(text)
    assert_completed(run)
    _assert_parse_identity(p, run)
    checks = evaluate(p, run)
    f_missed = [j for j in range(FRAMES) if f_ratio(checks, j) > 1]
    l_missed = [k for k in range(len(checks.l_bound)) if l_ratio(checks, k) > 1]
    c_missed = [k for k in range(len(checks.c_bound)) if c_ratio(checks, k) > 1]
    assert not f_missed, f"F missed at frames {f_missed[:10]}"
    assert not l_missed, f"L missed at exchanges {l_missed[:10]}"
    assert not c_missed, f"C missed at exchanges {c_missed[:10]}"
    assert all(checks.request_ok)
    assert checks.sequence_ok


def _assert_parse_identity(p: ref.Params, run: Run) -> None:
    """§7 (bitwise, §16 P-03): the doubles the sim parsed are float(text)."""
    assert _bits(run.dt) == _bits(float(p.h))
    for i in range(p.n):
        for j in range(p.n):
            assert _bits(run.signals[f"v0.linear_plant.a.{i}.{j}"]) == _bits(float(p.a[i][j]))
        assert _bits(run.signals[f"v0.linear_plant.b.{i}.0"]) == _bits(float(p.b[i][0]))
        assert _bits(run.states[0][i]) == _bits(float(p.x0[i]))
    for i, k in enumerate(p.k):
        assert _bits(run.signals[f"transport.peer.k.{i}"]) == _bits(float(k))
    assert _bits(run.signals["transport.peer.g"]) == _bits(float(p.g))
    assert _bits(run.signals["transport.peer.dt"]) == _bits(float(p.peer_dt))
    assert run.signals["transport.peer.divisor"] == float(p.peer_divisor)
    assert _bits(run.signals["transport.peer.dt"]) == _bits(run.dt)


def test_sampled_data_two_runs_are_byte_identical() -> None:
    """The M7b invariant: two runs of one config give identical recorders and trajectories."""
    first = simulate(nominal_text(10))
    second = simulate(nominal_text(10))
    assert first.recorder == second.recorder
    assert [[_bits(v) for v in x] for x in first.states] == [
        [_bits(v) for v in x] for x in second.states
    ]


# --- The negative controls (R1, R3) ---


def _assert_exchange_frames_fail(checks: Checks, windows: tuple[tuple[int, int], ...]) -> None:
    low = [k for k in _window(windows) if f_ratio(checks, k * CONTROL_DIVISOR) < WINDOW_MARGIN]
    assert not low, f"F below the window margin at exchanges {low[:10]}"


def _assert_c_fails(checks: Checks, first: int, windows: tuple[tuple[int, int], ...]) -> None:
    early = [k for k in range(first) if c_ratio(checks, k) > 1]
    assert not early, f"C failed before its registered first failure, at {early}"
    first_margin = c_ratio(checks, first)
    assert first_margin >= FIRST_FAILURE_MARGIN, (
        f"C's first failure margin 10^{_log10(first_margin):.1f}"
    )
    low = [k for k in _window(windows) if c_ratio(checks, k) < WINDOW_MARGIN]
    assert not low, f"C below the window margin at exchanges {low[:10]}"


def _assert_l_passes(checks: Checks) -> None:
    assert all(l_ratio(checks, k) <= 1 for k in range(len(checks.l_bound)))


def _plant_control(lines: tuple[str, ...]) -> tuple[Run, Checks]:
    text = config_text(divisor=CONTROL_DIVISOR, plant_lines=lines)
    run = simulate(text)
    assert_completed(run)
    # §8: parse identity and completion for every run, against the run's own config text (R-002).
    _assert_parse_identity(ref.Params(text), run)
    return run, evaluate(ref.Params(nominal_text(CONTROL_DIVISOR)), run)


def test_sampled_data_late_command_control_fails_as_registered() -> None:
    """§9, late command: F at exchange frames, C from k = 1; F elsewhere, L, the requests pass."""
    _, checks = _plant_control(("test_defect: late_command",))
    assert f_ratio(checks, 0) >= FIRST_FAILURE_MARGIN
    _assert_exchange_frames_fail(checks, LATE_F_WINDOWS)
    off = [j for j in range(FRAMES) if j % CONTROL_DIVISOR != 0 and f_ratio(checks, j) > 1]
    assert not off, f"F failed at frames with no exchange: {off[:10]}"
    _assert_c_fails(checks, LATE_C_FIRST, LATE_C_WINDOWS)
    _assert_l_passes(checks)
    assert all(checks.request_ok)
    assert checks.sequence_ok


def test_sampled_data_long_hold_control_fails_as_registered() -> None:
    """§9, long hold: F at frame 1010 only, C from k = 102; L and the requests pass."""
    _, checks = _plant_control(("test_defect: long_hold", f"test_defect_frame: {LONG_HOLD_FRAME}"))
    assert f_ratio(checks, LONG_HOLD_FRAME) >= FIRST_FAILURE_MARGIN
    others = [j for j in range(FRAMES) if j != LONG_HOLD_FRAME and f_ratio(checks, j) > 1]
    assert not others, f"F failed at frames other than {LONG_HOLD_FRAME}: {others[:10]}"
    _assert_c_fails(checks, LONG_C_FIRST, LONG_C_WINDOWS)
    _assert_l_passes(checks)
    assert all(checks.request_ok)
    assert checks.sequence_ok


def test_sampled_data_stage_switch_control_fails_as_registered() -> None:
    """§9, stage switch: F at exchange frames, C from k = 1; F elsewhere, L, the requests pass."""
    _, checks = _plant_control(("test_defect: stage_one_previous",))
    assert f_ratio(checks, 0) >= FIRST_FAILURE_MARGIN
    _assert_exchange_frames_fail(checks, STAGE_F_WINDOWS)
    off = [j for j in range(FRAMES) if j % CONTROL_DIVISOR != 0 and f_ratio(checks, j) > 1]
    assert not off, f"F failed at frames with no exchange: {off[:10]}"
    _assert_c_fails(checks, STAGE_C_FIRST, STAGE_C_WINDOWS)
    _assert_l_passes(checks)
    assert all(checks.request_ok)
    assert checks.sequence_ok


def test_sampled_data_wrong_divisor_control_fails_as_registered() -> None:
    """§9, wrong divisor: the requests from exchange 1, the sequence, C from k = 2; F, L pass."""
    text = config_text(divisor=WRONG_DIVISOR, gains=GAINS[CONTROL_DIVISOR][1])
    run = simulate(text)
    assert_completed(run)
    # §8: parse identity against this run's own text (R-002); the reference stays nominal N = 10's.
    _assert_parse_identity(ref.Params(text), run)
    checks = evaluate(ref.Params(nominal_text(CONTROL_DIVISOR)), run)
    assert checks.request_ok[0]
    assert not any(checks.request_ok[1:])
    # The second failing object (P-01): 182 requests against 201, at frames 0, 11, 22, ...
    assert not checks.sequence_ok
    assert len(run.requests) == WRONG_EXCHANGES
    assert len(run.replies) == WRONG_EXCHANGES
    assert [frame for frame, _, _, _ in run.requests] == [
        k * WRONG_DIVISOR for k in range(WRONG_EXCHANGES)
    ]
    assert all(f_ratio(checks, j) <= 1 for j in range(FRAMES))
    _assert_l_passes(checks)
    _assert_c_fails(checks, WRONG_C_FIRST, WRONG_C_WINDOWS)


def test_sampled_data_order_control_fails_as_registered() -> None:
    """§9 and R1, the order control: F on x_1 passes before frame 1010, fails at 1010 to 1029."""
    text = order_text()
    run = simulate(text)
    assert_completed(run)
    _assert_parse_identity(ref.Params(text), run)  # §8 (R-002)
    checks = evaluate(ref.Params(text), run)
    assert checks.sequence_ok  # §9 (P-01): every control but the wrong divisor passes it (R-001)
    x1 = range(1)
    early = [j for j in range(ORDER_FIRST_FRAME) if f_ratio(checks, j, x1) > 1]
    assert not early, f"F on x_1 failed before frame {ORDER_FIRST_FRAME}: {early[:10]}"
    assert f_ratio(checks, ORDER_FIRST_FRAME, x1) >= FIRST_FAILURE_MARGIN
    low = [
        j
        for j in range(ORDER_FRAMES[0], ORDER_FRAMES[1] + 1)
        if f_ratio(checks, j, x1) < ORDER_MARGIN
    ]
    assert not low, f"F on x_1 below the order margin at frames {low}"


# --- The peer's guards ---


def test_sampled_data_peer_refuses_a_frame_off_its_own_divisor() -> None:
    run = simulate(config_text(divisor=11, peer_divisor=10))
    assert run.halt is not None
    assert run.halt.check == _core.HealthCheck.TRANSPORT
    assert run.halt.frame == 11
    assert run.halt.source == "astra_sw_to_sim_v1.elevator"


def test_sampled_data_peer_refuses_a_time_off_its_own_dt() -> None:
    run = simulate(config_text(peer_dt="0.002"))
    assert run.halt is not None
    assert run.halt.check == _core.HealthCheck.TRANSPORT
    assert run.halt.frame == 10


@pytest.mark.parametrize(
    ("text", "path"),
    [
        (config_text(plant_lines=("test_defect: early_command",)), "test_defect"),
        (config_text(peer_state="v0.no_such_model.state"), "transport.peer.state"),
        (config_text(schedule="5"), "transport.peer.schedule"),
        (config_text(schedule='"[{ frame: 1007, reference: 1.0 }]"'), "transport.peer.schedule"),
    ],
    ids=["unknown-defect", "missing-state-signal", "scalar-schedule", "quoted-schedule"],
)
def test_sampled_data_config_errors_name_their_key(text: str, path: str) -> None:
    with pytest.raises(_core.ConfigError, match=re.escape(path)):
        _core.Simulation.from_config(
            _core.Config.from_string(text, "test_sampled_data"), _core.default_plugin_dir()
        )
