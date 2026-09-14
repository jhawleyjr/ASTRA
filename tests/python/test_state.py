"""The ADR-0003 state layout as Python sees it (mirrors tests/unit/kernel/state_test.cpp)."""

import numpy as np

from astra import _core


def test_layout_partitions_thirteen_states_as_adr_0003_says() -> None:
    assert _core.STATE_SIZE == 13
    assert (_core.POSITION, _core.VELOCITY, _core.QUATERNION, _core.ANGULAR_RATE) == (0, 3, 6, 10)
    assert _core.POSITION + _core.POSITION_SIZE == _core.VELOCITY
    assert _core.VELOCITY + _core.VELOCITY_SIZE == _core.QUATERNION
    assert _core.QUATERNION + _core.QUATERNION_SIZE == _core.ANGULAR_RATE
    assert _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE == _core.STATE_SIZE


def test_zero_state_is_a_c_contiguous_float64_vector() -> None:
    x = _core.zero_state()
    assert x.shape == (_core.STATE_SIZE,)
    assert x.dtype == np.float64
    assert x.flags["C_CONTIGUOUS"]
    assert not x.any()


def test_identity_state_sets_only_the_scalar_part_of_the_quaternion() -> None:
    x = _core.identity_state()
    q = x[_core.QUATERNION : _core.QUATERNION + _core.QUATERNION_SIZE]
    assert q.tolist() == [1.0, 0.0, 0.0, 0.0]
    assert x.sum() == 1.0


def test_each_call_returns_a_fresh_array() -> None:
    first = _core.zero_state()
    first[_core.POSITION] = 1.0
    assert not _core.zero_state().any()
