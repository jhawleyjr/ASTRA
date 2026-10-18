"""The exact sampled-data reference of M7b's oracle (a).

See docs/plans/m7b-pr2-sampled-data.md, §3 and §7.

Independence (ADR-0034 Decision 2; the M7b plan's amendment 4). This module imports ``fractions``,
``math`` and ``yaml``, and nothing from ``astra``; test_sampled_data.py checks that by parsing it.
It reads every parameter from the config text with PyYAML's ``BaseLoader``, under which every
scalar is a string, and converts each with ``float()`` or ``int()`` itself, never from the sim. Its
arithmetic is exact (``fractions.Fraction``): Phi and Gamma are the closed-form zero-order-hold
sums, never an integration. The only knowledge it has of the code is the operation order that F's
and L's bounds charge, read from ``core/include/astra/kernel/integrator.hpp:109-124``, the plant's
``integrate_state`` and the peer's ``publish`` (the plan's §5).

Every check compares exactly, as rationals (the plan's §7): F's bound is tight by construction at a
rounding tie (§10).
"""

import math
from fractions import Fraction

import yaml

# The plan's §7: the unit roundoff of IEEE double and half the smallest subnormal (a product's
# underflow; a sum of doubles is exact on underflow).
U = Fraction(1, 2**53)
ETA = Fraction(1, 2**1075)

Vector = list[Fraction]
Matrix = list[list[Fraction]]


def _identity(n: int) -> Matrix:
    return [[Fraction(int(i == j)) for j in range(n)] for i in range(n)]


def _zeros(rows: int, cols: int) -> Matrix:
    return [[Fraction(0)] * cols for _ in range(rows)]


def matmul(a: Matrix, b: Matrix) -> Matrix:
    return [
        [sum((a[i][k] * b[k][j] for k in range(len(b))), Fraction(0)) for j in range(len(b[0]))]
        for i in range(len(a))
    ]


def matvec(a: Matrix, x: Vector) -> Vector:
    return [sum((row[k] * x[k] for k in range(len(x))), Fraction(0)) for row in a]


def _add(a: Matrix, b: Matrix) -> Matrix:
    return [[a[i][j] + b[i][j] for j in range(len(a[0]))] for i in range(len(a))]


def _scale(s: Fraction, a: Matrix) -> Matrix:
    return [[s * v for v in row] for row in a]


def _absolute(a: Matrix) -> Matrix:
    return [[abs(v) for v in row] for row in a]


def inverse(m: Matrix) -> Matrix:
    """The exact inverse, by Gauss-Jordan elimination over the rationals."""
    n = len(m)
    rows = [row[:] + [Fraction(int(i == j)) for j in range(n)] for i, row in enumerate(m)]
    for c in range(n):
        pivot = next(r for r in range(c, n) if rows[r][c] != 0)
        rows[c], rows[pivot] = rows[pivot], rows[c]
        p = rows[c][c]
        rows[c] = [v / p for v in rows[c]]
        for r in range(n):
            if r != c and rows[r][c] != 0:
                f = rows[r][c]
                rows[r] = [v - f * w for v, w in zip(rows[r], rows[c], strict=True)]
    return [row[n:] for row in rows]


def characteristic_polynomial(m: Matrix) -> Vector:
    """det(zI - m)'s coefficients, leading first, exactly (Faddeev-LeVerrier)."""
    n = len(m)
    coefficients = [Fraction(1)]
    mk = _zeros(n, n)
    for k in range(1, n + 1):
        mk = _add(matmul(m, mk), _scale(coefficients[-1], _identity(n)))
        trace = sum((matmul(m, mk)[i][i] for i in range(n)), Fraction(0))
        coefficients.append(-trace / k)
    return coefficients


def _exact(text: str) -> Fraction:
    """A config scalar as the double strtod gives, exactly (float() rounds as strtod does)."""
    return Fraction(float(text))


class Params:
    """Oracle (a)'s parameters, read from one config's text."""

    def __init__(self, text: str) -> None:
        # BaseLoader resolves no tags and builds only strings, lists and maps: every scalar stays
        # the config's own text, which float() and int() then read (the plan's §3).
        doc = yaml.load(text, Loader=yaml.BaseLoader)
        self.h = _exact(doc["sim"]["dt"])
        self.frames = int(doc["sim"]["frames"])
        plant = next(
            m for m in doc["vehicle"]["models"] if m["plugin"] == "tests/plugins/linear_plant"
        )
        self.a: Matrix = [[_exact(v) for v in row] for row in plant["a"]]
        self.b: Matrix = [[_exact(v) for v in row] for row in plant["b"]]
        self.x0: Vector = [_exact(v) for v in plant["x0"]]
        self.n = len(self.a)
        transport = doc["transport"]
        self.divisor = int(transport["divisor"])
        self.limits = {
            key: tuple(_exact(v) for v in pair) for key, pair in transport["limits"].items()
        }
        peer = transport["peer"]
        self.peer_divisor = int(peer["divisor"])
        self.peer_dt = _exact(peer["dt"])
        self.k: Vector = [_exact(v) for v in peer["k"]]
        self.g = _exact(peer["g"])
        self.reference0 = _exact(peer["reference"])
        self.schedule = [
            (int(e["frame"]), _exact(e["reference"])) for e in peer.get("schedule", [])
        ]

    def reference_at(self, frame: int) -> Fraction:
        """r in force at an exchange at frame: from the first exchange at or after each entry."""
        r = self.reference0
        for entry_frame, value in self.schedule:
            if entry_frame <= frame:
                r = value
        return r

    def chain(self) -> tuple[Fraction, Fraction]:
        """(a, b) for A = a S and B = b e_n, m = 1: the structure F's bound program assumes (§5)."""
        n = self.n
        a = self.a[0][1] if n > 1 else Fraction(0)
        for i in range(n):
            for j in range(n):
                expected = a if j == i + 1 else Fraction(0)
                if self.a[i][j] != expected:
                    raise ValueError(f"A is not a times the upper shift at ({i}, {j})")
        if any(len(row) != 1 for row in self.b):
            raise ValueError("B must have one column")
        b = self.b[n - 1][0]
        if any(self.b[i][0] != 0 for i in range(n - 1)) or b == 0:
            raise ValueError("B is not b times e_n")
        return a, b


def zoh(p: Params, steps: int) -> tuple[Matrix, Vector]:
    """(Phi_j, Gamma_j) for j = steps frames: the exact zero-order hold (§3).

    Phi_j = sum_{m<n} (j h A)^m / m! and Gamma_j = sum_{m<n} (j h)^(m+1) A^m B / (m+1)!. Finite,
    because A^n = 0, which is asserted.
    """
    n = p.n
    power = _identity(n)
    for _ in range(n):
        power = matmul(power, p.a)
    if any(v != 0 for row in power for v in row):
        raise ValueError("A is not nilpotent of index at most n")
    jh = steps * p.h
    phi = _zeros(n, n)
    gamma = [Fraction(0)] * n
    power = _identity(n)
    for m in range(n):
        phi = _add(phi, _scale(jh**m / math.factorial(m), power))
        column = matvec(power, [row[0] for row in p.b])
        gamma = [
            g + jh ** (m + 1) / math.factorial(m + 1) * c
            for g, c in zip(gamma, column, strict=True)
        ]
        power = matmul(power, p.a)
    return phi, gamma


def rk4_tableau(p: Params) -> tuple[Matrix, Vector]:
    """Classical RK4's one-frame map for x' = A x + B u with u held, in exact arithmetic (R1).

    Columns of the returned (Tx, Tu) are the tableau applied to each basis state with u = 0, and to
    u = 1 with x = 0. With h/6 exact, this is T4(hM), which equals the exact flow when n <= 4.
    """
    n = p.n
    bcol = [row[0] for row in p.b]
    h = p.h

    def f(z: Vector, u: Fraction) -> Vector:
        return [v + bi * u for v, bi in zip(matvec(p.a, z), bcol, strict=True)]

    def step(y: Vector, u: Fraction) -> Vector:
        k1 = f(y, u)
        k2 = f([y[i] + h / 2 * k1[i] for i in range(n)], u)
        k3 = f([y[i] + h / 2 * k2[i] for i in range(n)], u)
        k4 = f([y[i] + h * k3[i] for i in range(n)], u)
        return [y[i] + h / 6 * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]) for i in range(n)]

    columns = [step([Fraction(int(i == j)) for i in range(n)], Fraction(0)) for j in range(n)]
    tx = [[columns[j][i] for j in range(n)] for i in range(n)]
    tu = step([Fraction(0)] * n, Fraction(1))
    return tx, tu


def c6(p: Params) -> Fraction:
    """The core's dt_ / 6.0 (integrator.hpp:124): one correctly rounded IEEE division."""
    return Fraction(float(p.h) / 6.0)


# F's running bound (the plan's §7). A node is (exact value, E), E bounding |computed - exact|.
_Node = tuple[Fraction, Fraction]


def _product(kappa: Fraction, x: _Node) -> _Node:
    value = kappa * x[0]
    carried = abs(kappa) * x[1]
    return value, carried + U * (abs(value) + carried) + ETA


def _sum(x: _Node, y: _Node) -> _Node:
    value = x[0] + y[0]
    carried = x[1] + y[1]
    return value, carried + U * (abs(value) + carried)


def _double(x: _Node) -> _Node:
    return 2 * x[0], 2 * x[1]


def f_bound(p: Params, x: Vector, u: Fraction) -> Vector:
    """F_i(j) over §5's program, evaluated exactly on the recorded x = x_sim(j) and u = u_hold(j).

    The program is integrator.hpp:109-124 around the plant's derivative, which for A = a S and
    B = b e_n is exactly fl(a x_{i+1}) for i < n and fl(b u) for i = n (zero products and sums with
    a zero operand are exact). The output adds amendment 6's exact |c6 - h/6| |s_i|.
    """
    a, b = p.chain()
    n = p.n
    half = p.h / 2  # integrator.hpp:109, exact
    c = c6(p)
    y: list[_Node] = [(xi, Fraction(0)) for xi in x]
    bu = _product(b, (u, Fraction(0)))

    def derivative(z: list[_Node]) -> list[_Node]:
        return [_product(a, z[i + 1]) for i in range(n - 1)] + [bu]

    k1 = derivative(y)  # :110
    k2 = derivative([_sum(y[i], _product(half, k1[i])) for i in range(n)])  # :111-114
    k3 = derivative([_sum(y[i], _product(half, k2[i])) for i in range(n)])  # :115-118
    k4 = derivative([_sum(y[i], _product(p.h, k3[i])) for i in range(n)])  # :119-122
    gap = abs(c - p.h / 6)
    out: Vector = []
    for i in range(n):  # :124, ((k1 + 2 k2) + 2 k3) + k4, then times c6, then added to y
        s = _sum(_sum(_sum(k1[i], _double(k2[i])), _double(k3[i])), k4[i])
        updated = _sum(y[i], _product(c, s))
        out.append(updated[1] + gap * abs(s[0]))
    return out


def gamma(m: int) -> Fraction:
    return m * U / (1 - m * U)


def law(p: Params, x: Vector, r: Fraction) -> Fraction:
    """G r - K x, exactly."""
    return p.g * r - sum((ki * xi for ki, xi in zip(p.k, x, strict=True)), Fraction(0))


def l_bound(p: Params, x: Vector, r: Fraction) -> Fraction:
    """L's bound (§7): the recursive inner-product bound of n + 1 terms, plus underflow."""
    n = len(p.k)
    g = gamma(n + 1)
    terms = sum((abs(ki * xi) for ki, xi in zip(p.k, x, strict=True)), Fraction(0)) + abs(p.g * r)
    return g * terms + (1 + g) * (n + 1) * ETA


def closed_loop(p: Params) -> tuple[Matrix, Vector]:
    """(Phi_cl, Gamma_N G) at the peer's divisor N: Phi_cl = Phi_N - Gamma_N K."""
    phi, gam = zoh(p, p.peer_divisor)
    n = p.n
    phi_cl = [[phi[i][j] - gam[i] * p.k[j] for j in range(n)] for i in range(n)]
    return phi_cl, [gi * p.g for gi in gam]


def _round_up(value: Fraction, bits: int) -> Fraction:
    return Fraction(math.ceil(value * 2**bits), 2**bits)


def s_bar(p: Params, round_bits: int) -> tuple[int, Matrix, Matrix]:
    """(I, S-bar exact, S-bar rounded) for C's bound (§7): S-bar >= sum_m |Phi_cl^m| elementwise.

    I is the smallest m with ||Phi_cl^m||_inf <= 1/2, and
    S-bar = (sum_{m<I} |Phi_cl^m|)(I - |Phi_cl^I|)^-1, exact. The rounded S-bar replaces each entry
    v by ceil(v 2^round_bits) / 2^round_bits (§7, P-02), so C's bound can only grow; C uses it.
    """
    phi_cl, _ = closed_loop(p)
    n = p.n
    power = _identity(n)
    total = _zeros(n, n)
    for m in range(1, 513):
        total = _add(total, _absolute(power))
        power = matmul(phi_cl, power)
        if max(sum(abs(v) for v in row) for row in power) <= Fraction(1, 2):
            tail = inverse(
                [[Fraction(int(i == j)) - abs(power[i][j]) for j in range(n)] for i in range(n)]
            )
            exact = matmul(total, tail)
            return m, exact, [[_round_up(v, round_bits) for v in row] for row in exact]
    raise ValueError("||Phi_cl^m|| did not reach 1/2 by m = 512")


def reference_states(p: Params, count: int, grid_bits: int) -> list[Vector]:
    """C's reference x~[0..count-1] (§7): the exact recursion, rounded to a 2^-grid_bits grid."""
    phi_cl, gam_g = closed_loop(p)
    x = list(p.x0)
    states = [x]
    scale = 2**grid_bits
    for k in range(count - 1):
        r = p.reference_at(k * p.peer_divisor)
        nxt = [v + gi * r for v, gi in zip(matvec(phi_cl, x), gam_g, strict=True)]
        x = [Fraction(round(v * scale), scale) for v in nxt]
        states.append(x)
    return states


def c_bounds(
    p: Params,
    count: int,
    f_bounds: list[Vector],
    l_bounds: list[Fraction],
    sbar: Matrix,
    grid_bits: int,
) -> list[Vector]:
    """C(k) for k = 0..count-1 (§7): S-bar (w-bar_{k-1} + 2^-(grid_bits+1)), elementwise.

    w_j = |Gamma_N| L(j) + sum_{i<N} |Phi_1^(N-1-i)| F(j N + i), with L(j) the bound at the run's
    j-th recorded exchange (0 past the run's last exchange), and w-bar_{k-1} the running maximum.
    """
    n = p.n
    big_n = p.peer_divisor
    phi1, _ = zoh(p, 1)
    _, gam_n = zoh(p, big_n)
    powers = [_identity(n)]
    for _ in range(big_n - 1):
        powers.append(matmul(phi1, powers[-1]))
    absolute = [_absolute(m) for m in powers]
    half_grid = Fraction(1, 2 ** (grid_bits + 1))
    wbar = [Fraction(0)] * n
    bounds: list[Vector] = []
    for k in range(count):
        if k > 0:
            j = k - 1
            injected = [abs(g) * (l_bounds[j] if j < len(l_bounds) else Fraction(0)) for g in gam_n]
            for i in range(big_n):
                frame_bound = f_bounds[j * big_n + i]
                carried = matvec(absolute[big_n - 1 - i], frame_bound)
                injected = [v + c for v, c in zip(injected, carried, strict=True)]
            wbar = [max(v, w) for v, w in zip(wbar, injected, strict=True)]
        bounds.append(matvec(sbar, [v + half_grid for v in wbar]))
    return bounds
