"""FlightGear's FGNetFDM, version 24, transcribed (docs/plans/m5-tooling.md Q21; a human-owned
oracle).

Source: FlightGear 2024.1.7, the SourceForge release source. Two files, read at the main checkout's
gitignored docs/refs/ (docs/lessons.md 2026-09-17):

- ``net_fdm.hxx`` (``src/Network/net_fdm.hxx`` in the release; public domain): the struct. Field
  order, C types, array lengths and the unit comments below are transcribed from its lines 34 to 124
  (``class FGNetFDM``), each field citing its own line (``h:<line>``).
- ``native_structs.cxx`` (``src/Network/native_structs.cxx``; GPL-2.0-or-later, read, not copied):
  how FlightGear converts and reads the packet. ``FGFDM2Props<FGNetFDM>`` (line 275) is the receive
  side: each field's ``reader`` cites the line where FlightGear stores it and the property it sets,
  which is how each unit below was checked (``cxx:<line>``).

Nothing here is computed from ASTRA code or from memory. The layout (``offset``, ``SIZE``,
``ALIGNMENT``) is the x86-64 layout of the header compiled with g++ and a one-line stub for
``<simgear/misc/stdint.hxx>``; ``scripts/fgnetfdm_layout_check.py`` recomputes it from the header
by hand (never run by ``just check``, the ADR-0013 generator pattern).

Units. ``unit`` is the unit this table states for the wire value. Where the header comment names a
unit, ``unit`` is that unit and the reader agrees. Where the header is silent or ambiguous, ``unit``
is the reader's (Joe's decision, M5 PR 8 plan review) and ``note`` says so: ``vcas`` (knots,
cxx:391), ``v_body_u/v/w`` (ft/s, cxx:396 to 398) and ``v_north/east/down`` (NED ft/s, cxx:393
to 395).
``None`` means the field carries no physical unit (a count, a state code, a normalized position) or
FlightGear never stores it. No field's header unit disagrees with its reader.
"""

from typing import NamedTuple

RELEASE = "FlightGear 2024.1.7 (SourceForge release source)"
NET_FDM_HXX = "src/Network/net_fdm.hxx"
NET_FDM_HXX_SHA256 = "7b25b342fcbeaa15fb7f76a61277ac429dd8ea9d1b4650839f8523eff5a25460"
NATIVE_STRUCTS_CXX = "src/Network/native_structs.cxx"
NATIVE_STRUCTS_CXX_SHA256 = "ea8fda121dd4a9e8e3377a311e3e2ea7a7e5968231b6676afd811948d87e1440"

# h:28: const uint32_t FG_NET_FDM_VERSION = 24; the reader applies a packet only when its version
# equals this (cxx:354), and logs a mismatch otherwise (cxx:478 to 485).
FG_NET_FDM_VERSION = 24
VERSION_LINE = 28

# h:39 to 41: the enum sizing the arrays.
FG_MAX_ENGINES = 4
FG_MAX_WHEELS = 3
FG_MAX_TANKS = 4

# sizeof(FGNetFDM) and alignof(FGNetFDM) on x86-64 (g++, measured). The members below are laid out
# with no implicit padding: the header's own `padding` member (h:45) aligns the first double.
SIZE = 408
ALIGNMENT = 8

# Network byte order, every field whole. The sender converts in FGProps2FDM<FGNetFDM> (cxx:89) when
# net_byte_order is set (cxx:197 to 271): htonl for every uint32_t and for warp (cxx:199, 231, 242,
# 247, 250, 255, 257, 258); htond for each double (defined at cxx:56: on a little-endian host it
# swaps the two 32-bit halves and applies htonl to each, reversing all 8 bytes); htonf for each
# float (cxx:73: htonl of the float's 32 bits). The reader undoes it the same way (cxx:278 to 352).
BYTE_ORDER = "big"
BYTE_ORDER_LINES = (56, 73, 199)


class Field(NamedTuple):
    name: str
    ctype: str  # "uint32_t", "int32_t", "float" or "double"
    length: int  # 1 for a scalar; the array length for an array
    offset: int  # bytes from the start of the struct
    header_line: int
    header_comment: str  # verbatim, "" when the line has none
    unit: str | None
    reader: str  # FGFDM2Props<FGNetFDM>: the line(s) and what FlightGear does with the value
    note: str = ""


FIELDS: tuple[Field, ...] = (
    Field(
        name="version",
        ctype="uint32_t",
        length=1,
        offset=0,
        header_line=44,
        header_comment="increment when data values change",
        unit=None,
        reader="cxx:280 ntohl; cxx:354 applied only if == FG_NET_FDM_VERSION",
    ),
    Field(
        name="padding",
        ctype="uint32_t",
        length=1,
        offset=4,
        header_line=45,
        header_comment="padding",
        unit=None,
        reader="not read",
    ),
    # Positions (h:47)
    Field(
        name="longitude",
        ctype="double",
        length=1,
        offset=8,
        header_line=48,
        header_comment="geodetic (radians)",
        unit="rad",
        reader="cxx:361-362 position/longitude-deg = longitude * SG_RADIANS_TO_DEGREES",
    ),
    Field(
        name="latitude",
        ctype="double",
        length=1,
        offset=16,
        header_line=49,
        header_comment="geodetic (radians)",
        unit="rad",
        reader="cxx:359-360 position/latitude-deg = latitude * SG_RADIANS_TO_DEGREES",
    ),
    Field(
        name="altitude",
        ctype="double",
        length=1,
        offset=24,
        header_line=50,
        header_comment="above sea level (meters)",
        unit="m",
        reader="cxx:363-364 position/altitude-ft = altitude * SG_METER_TO_FEET",
    ),
    Field(
        name="agl",
        ctype="float",
        length=1,
        offset=32,
        header_line=51,
        header_comment="above ground level (meters)",
        unit="m",
        reader="cxx:366-374 if agl > -9000: position/altitude-agl-ft = agl * SG_METER_TO_FEET",
        note=(
            "at or below -9000 the reader computes AGL from altitude and its own "
            "environment/ground-elevation-m (cxx:370-373) instead"
        ),
    ),
    Field(
        name="phi",
        ctype="float",
        length=1,
        offset=36,
        header_line=52,
        header_comment="roll (radians)",
        unit="rad",
        reader="cxx:375-376 orientation/roll-deg = phi * SG_RADIANS_TO_DEGREES",
    ),
    Field(
        name="theta",
        ctype="float",
        length=1,
        offset=40,
        header_line=53,
        header_comment="pitch (radians)",
        unit="rad",
        reader="cxx:377-378 orientation/pitch-deg = theta * SG_RADIANS_TO_DEGREES",
    ),
    Field(
        name="psi",
        ctype="float",
        length=1,
        offset=44,
        header_line=54,
        header_comment="yaw or true heading (radians)",
        unit="rad",
        reader="cxx:379-380 orientation/heading-deg = psi * SG_RADIANS_TO_DEGREES",
    ),
    Field(
        name="alpha",
        ctype="float",
        length=1,
        offset=48,
        header_line=55,
        header_comment="angle of attack (radians)",
        unit="rad",
        reader="cxx:381-382 orientation/alpha-deg = alpha * SG_RADIANS_TO_DEGREES",
    ),
    Field(
        name="beta",
        ctype="float",
        length=1,
        offset=52,
        header_line=56,
        header_comment="side slip angle (radians)",
        unit="rad",
        reader="cxx:383-384 orientation/side-slip-rad = beta * SG_RADIANS_TO_DEGREES",
        note=(
            "the wire unit (rad) agrees; the reader stores degrees into a property named "
            "side-slip-rad, and the sender reads orientation/beta-deg (cxx:109-110): a "
            "FlightGear-internal naming inconsistency, not a wire disagreement"
        ),
    ),
    # Velocities (h:58)
    Field(
        name="phidot",
        ctype="float",
        length=1,
        offset=56,
        header_line=59,
        header_comment="roll rate (radians/sec)",
        unit="rad/s",
        reader="cxx:385-386 orientation/roll-rate-degps = phidot * SG_RADIANS_TO_DEGREES",
    ),
    Field(
        name="thetadot",
        ctype="float",
        length=1,
        offset=60,
        header_line=60,
        header_comment="pitch rate (radians/sec)",
        unit="rad/s",
        reader="cxx:387-388 orientation/pitch-rate-degps = thetadot * SG_RADIANS_TO_DEGREES",
    ),
    Field(
        name="psidot",
        ctype="float",
        length=1,
        offset=64,
        header_line=61,
        header_comment="yaw rate (radians/sec)",
        unit="rad/s",
        reader="cxx:389-390 orientation/yaw-rate-degps = psidot * SG_RADIANS_TO_DEGREES",
    ),
    Field(
        name="vcas",
        ctype="float",
        length=1,
        offset=68,
        header_line=62,
        header_comment="calibrated airspeed",
        unit="kt",
        reader="cxx:391 velocities/airspeed-kt = vcas",
        note=(
            "the header gives no unit; knots per the reader (cxx:391), and the sender fills it "
            "from the same property (cxx:118)"
        ),
    ),
    Field(
        name="climb_rate",
        ctype="float",
        length=1,
        offset=72,
        header_line=63,
        header_comment="feet per second",
        unit="ft/s",
        reader="cxx:392 velocities/vertical-speed-fps = climb_rate",
    ),
    Field(
        name="v_north",
        ctype="float",
        length=1,
        offset=76,
        header_line=64,
        header_comment="north velocity in local/body frame, fps",
        unit="ft/s",
        reader="cxx:393 velocities/speed-north-fps = v_north",
        note="the header says local/body; the reader stores it as the local NED north component",
    ),
    Field(
        name="v_east",
        ctype="float",
        length=1,
        offset=80,
        header_line=65,
        header_comment="east velocity in local/body frame, fps",
        unit="ft/s",
        reader="cxx:394 velocities/speed-east-fps = v_east",
        note="the header says local/body; the reader stores it as the local NED east component",
    ),
    Field(
        name="v_down",
        ctype="float",
        length=1,
        offset=84,
        header_line=66,
        header_comment="down/vertical velocity in local/body frame, fps",
        unit="ft/s",
        reader="cxx:395 velocities/speed-down-fps = v_down",
        note="the header says local/body; the reader stores it as the local NED down component",
    ),
    Field(
        name="v_body_u",
        ctype="float",
        length=1,
        offset=88,
        header_line=67,
        header_comment="ECEF velocity in body frame",
        unit="ft/s",
        reader="cxx:396 velocities/uBody-fps = v_body_u",
        note=(
            "the header gives no unit; ft/s per the reader. Earth-relative (ECEF) velocity in "
            "body axes, not air-relative (Joe: filled from the state's v_b)"
        ),
    ),
    Field(
        name="v_body_v",
        ctype="float",
        length=1,
        offset=92,
        header_line=68,
        header_comment="ECEF velocity in body frame",
        unit="ft/s",
        reader="cxx:397 velocities/vBody-fps = v_body_v",
        note="the header gives no unit; ft/s per the reader. Earth-relative, as v_body_u",
    ),
    Field(
        name="v_body_w",
        ctype="float",
        length=1,
        offset=96,
        header_line=69,
        header_comment="ECEF velocity in body frame",
        unit="ft/s",
        reader="cxx:398 velocities/wBody-fps = v_body_w",
        note="the header gives no unit; ft/s per the reader. Earth-relative, as v_body_u",
    ),
    # Accelerations (h:71)
    Field(
        name="A_X_pilot",
        ctype="float",
        length=1,
        offset=100,
        header_line=72,
        header_comment="X accel in body frame ft/sec^2",
        unit="ft/s^2",
        reader="cxx:399-400 accelerations/pilot/x-accel-fps_sec = A_X_pilot",
    ),
    Field(
        name="A_Y_pilot",
        ctype="float",
        length=1,
        offset=104,
        header_line=73,
        header_comment="Y accel in body frame ft/sec^2",
        unit="ft/s^2",
        reader="cxx:401-402 accelerations/pilot/y-accel-fps_sec = A_Y_pilot",
    ),
    Field(
        name="A_Z_pilot",
        ctype="float",
        length=1,
        offset=108,
        header_line=74,
        header_comment="Z accel in body frame ft/sec^2",
        unit="ft/s^2",
        reader="cxx:403-404 accelerations/pilot/z-accel-fps_sec = A_Z_pilot",
    ),
    # Stall (h:76)
    Field(
        name="stall_warning",
        ctype="float",
        length=1,
        offset=112,
        header_line=77,
        header_comment="0.0 - 1.0 indicating the amount of stall",
        unit=None,
        reader="cxx:406 /sim/alarms/stall-warning = stall_warning",
    ),
    Field(
        name="slip_deg",
        ctype="float",
        length=1,
        offset=116,
        header_line=78,
        header_comment="slip ball deflection",
        unit=None,
        reader=(
            "cxx:407-409 /instrumentation/slip-skid-ball/indicated-slip-skid = slip_deg, and "
            "the ball's override is set true"
        ),
    ),
    # Engine status (h:82)
    Field(
        name="num_engines",
        ctype="uint32_t",
        length=1,
        offset=120,
        header_line=83,
        header_comment="Number of valid engines",
        unit=None,
        reader="cxx:311 ntohl; cxx:411 loop bound for the engine arrays",
        note="the reader bounds its loops by this count, not by FG_MAX_ENGINES (cxx:312, 411)",
    ),
    Field(
        name="eng_state",
        ctype="uint32_t",
        length=FG_MAX_ENGINES,
        offset=124,
        header_line=84,
        header_comment="Engine state (off, cranking, running)",
        unit=None,
        reader="cxx:418-427 0 off, 1 cranking, 2 running",
    ),
    Field(
        name="rpm",
        ctype="float",
        length=FG_MAX_ENGINES,
        offset=140,
        header_line=85,
        header_comment="Engine RPM rev/min",
        unit="rev/min",
        reader="cxx:429 engines/engine[i]/rpm",
    ),
    Field(
        name="fuel_flow",
        ctype="float",
        length=FG_MAX_ENGINES,
        offset=156,
        header_line=86,
        header_comment="Fuel flow gallons/hr",
        unit="gal/h",
        reader="cxx:430 engines/engine[i]/fuel-flow-gph",
    ),
    Field(
        name="fuel_px",
        ctype="float",
        length=FG_MAX_ENGINES,
        offset=172,
        header_line=87,
        header_comment="Fuel pressure psi",
        unit="psi",
        reader="cxx:431 engines/engine[i]/fuel-px-psi",
    ),
    Field(
        name="egt",
        ctype="float",
        length=FG_MAX_ENGINES,
        offset=188,
        header_line=88,
        header_comment="Exhuast gas temp deg F",
        unit="degF",
        reader="cxx:432 engines/engine[i]/egt-degf",
    ),
    Field(
        name="cht",
        ctype="float",
        length=FG_MAX_ENGINES,
        offset=204,
        header_line=89,
        header_comment="Cylinder head temp deg F",
        unit="degF",
        reader="cxx:433 engines/engine[i]/cht-degf",
    ),
    Field(
        name="mp_osi",
        ctype="float",
        length=FG_MAX_ENGINES,
        offset=220,
        header_line=90,
        header_comment="Manifold pressure",
        unit=None,
        reader="cxx:434 engines/engine[i]/mp-osi",
        note="neither the header nor the property name states a unit",
    ),
    Field(
        name="tit",
        ctype="float",
        length=FG_MAX_ENGINES,
        offset=236,
        header_line=91,
        header_comment="Turbine Inlet Temperature",
        unit=None,
        reader="cxx:435 engines/engine[i]/tit",
        note="neither the header nor the property name states a unit",
    ),
    Field(
        name="oil_temp",
        ctype="float",
        length=FG_MAX_ENGINES,
        offset=252,
        header_line=92,
        header_comment="Oil temp deg F",
        unit="degF",
        reader="cxx:436 engines/engine[i]/oil-temperature-degf",
    ),
    Field(
        name="oil_px",
        ctype="float",
        length=FG_MAX_ENGINES,
        offset=268,
        header_line=93,
        header_comment="Oil pressure psi",
        unit="psi",
        reader="cxx:437 engines/engine[i]/oil-pressure-psi",
    ),
    # Consumables (h:95)
    Field(
        name="num_tanks",
        ctype="uint32_t",
        length=1,
        offset=284,
        header_line=96,
        header_comment="Max number of fuel tanks",
        unit=None,
        reader="cxx:325 ntohl; cxx:440 loop bound for fuel_quantity",
        note="the reader bounds its loops by this count, not by FG_MAX_TANKS (cxx:326, 440)",
    ),
    Field(
        name="fuel_quantity",
        ctype="float",
        length=FG_MAX_TANKS,
        offset=288,
        header_line=97,
        header_comment="",
        unit="gal_us",
        reader="cxx:443 /consumables/fuel/tank[i]/level-gal_us",
        note="no header comment; the unit is the property name's",
    ),
    # Gear status (h:100)
    Field(
        name="num_wheels",
        ctype="uint32_t",
        length=1,
        offset=304,
        header_line=101,
        header_comment="",
        unit=None,
        reader="cxx:330 ntohl; cxx:446 loop bound for the gear arrays",
        note="the reader bounds its loops by this count, not by FG_MAX_WHEELS (cxx:331, 446)",
    ),
    Field(
        name="wow",
        ctype="uint32_t",
        length=FG_MAX_WHEELS,
        offset=308,
        header_line=102,
        header_comment="",
        unit=None,
        reader="cxx:448 /gear/gear[i]/wow",
    ),
    Field(
        name="gear_pos",
        ctype="float",
        length=FG_MAX_WHEELS,
        offset=320,
        header_line=103,
        header_comment="",
        unit=None,
        reader="cxx:449 /gear/gear[i]/position-norm",
    ),
    Field(
        name="gear_steer",
        ctype="float",
        length=FG_MAX_WHEELS,
        offset=332,
        header_line=104,
        header_comment="",
        unit=None,
        reader="cxx:450 /gear/gear[i]/steering-norm",
    ),
    Field(
        name="gear_compression",
        ctype="float",
        length=FG_MAX_WHEELS,
        offset=344,
        header_line=105,
        header_comment="",
        unit=None,
        reader="cxx:451 /gear/gear[i]/compression-norm",
    ),
    # Environment (h:107)
    Field(
        name="cur_time",
        ctype="uint32_t",
        length=1,
        offset=356,
        header_line=108,
        header_comment="current unix time",
        unit=None,
        reader="cxx:338 byte order only; its use is commented out (cxx:454-462)",
        note="h:109: FIXME: make this uint64_t before 2038",
    ),
    Field(
        name="warp",
        ctype="int32_t",
        length=1,
        offset=360,
        header_line=110,
        header_comment="offset in seconds to unix time",
        unit=None,
        reader="cxx:339 byte order only; its use is commented out (cxx:454-462)",
    ),
    Field(
        name="visibility",
        ctype="float",
        length=1,
        offset=364,
        header_line=111,
        header_comment="visibility in meters (for env. effects)",
        unit=None,
        reader="cxx:340 byte order only; never stored",
    ),
    # Control surface positions (normalized values) (h:113)
    Field(
        name="elevator",
        ctype="float",
        length=1,
        offset=368,
        header_line=114,
        header_comment="",
        unit=None,
        reader="cxx:465 /surface-positions/elevator-pos-norm",
    ),
    Field(
        name="elevator_trim_tab",
        ctype="float",
        length=1,
        offset=372,
        header_line=115,
        header_comment="",
        unit=None,
        reader="cxx:466-467 /surface-positions/elevator-trim-tab-pos-norm",
    ),
    Field(
        name="left_flap",
        ctype="float",
        length=1,
        offset=376,
        header_line=116,
        header_comment="",
        unit=None,
        reader="cxx:470 /surface-positions/flap-pos-norm",
    ),
    Field(
        name="right_flap",
        ctype="float",
        length=1,
        offset=380,
        header_line=117,
        header_comment="",
        unit=None,
        reader="cxx:471 /surface-positions/flap-pos-norm (overwrites left_flap's value)",
    ),
    Field(
        name="left_aileron",
        ctype="float",
        length=1,
        offset=384,
        header_line=118,
        header_comment="",
        unit=None,
        reader="cxx:472 /surface-positions/left-aileron-pos-norm",
    ),
    Field(
        name="right_aileron",
        ctype="float",
        length=1,
        offset=388,
        header_line=119,
        header_comment="",
        unit=None,
        reader="cxx:473 /surface-positions/right-aileron-pos-norm",
    ),
    Field(
        name="rudder",
        ctype="float",
        length=1,
        offset=392,
        header_line=120,
        header_comment="",
        unit=None,
        reader="cxx:474 /surface-positions/rudder-pos-norm",
    ),
    Field(
        name="nose_wheel",
        ctype="float",
        length=1,
        offset=396,
        header_line=121,
        header_comment="",
        unit=None,
        reader="cxx:475 /surface-positions/nose-wheel-pos-norm",
    ),
    Field(
        name="speedbrake",
        ctype="float",
        length=1,
        offset=400,
        header_line=122,
        header_comment="",
        unit=None,
        reader="cxx:476 /surface-positions/speedbrake-pos-norm",
    ),
    Field(
        name="spoilers",
        ctype="float",
        length=1,
        offset=404,
        header_line=123,
        header_comment="",
        unit=None,
        reader="cxx:477 /surface-positions/spoilers-pos-norm",
    ),
)
