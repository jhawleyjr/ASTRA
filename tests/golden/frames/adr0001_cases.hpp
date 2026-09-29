// ADR-0001 sign-pinning oracle. Human-owned: lives under tests/golden/ so guard-oracle.sh blocks
// agent edits. Every number below was computed (numpy, 2026-09-15, Cowork) from the closed forms
// typed out here, with no ASTRA code and no third-party quaternion library involved, so the test
// judges the implementation against the convention, not against itself. Compare with |a - b| <=
// kTolerance.
//
// clang-format off
// Conventions (ADR-0001): body FRD, nav NED, 3-2-1 Euler psi (yaw), theta (pitch), phi (roll) in
// rad. q_nb is Hamilton, scalar-first (w, x, y, z), rotates n -> b: v_b = C_nb v_n. Stevens &
// Lewis, Aircraft Control and Simulation, 3rd ed. (c = cos, s = sin, p = phi, t = theta, ps = psi):
//   C_nb from Euler (eq. 1.3-10):
//     [ ct*cps            ct*sps            -st   ]
//     [ sp*st*cps-cp*sps  sp*st*sps+cp*cps  sp*ct ]
//     [ cp*st*cps+sp*sps  cp*st*sps-sp*cps  cp*ct ]
//   q_nb from Euler (Example 1.8-1 eq. (1), p. 52, + sign), half angles a = phi/2, b = theta/2, c = psi/2:
//     w = ca*cb*cc + sa*sb*sc,  x = sa*cb*cc - ca*sb*sc,
//     y = ca*sb*cc + sa*cb*sc,  z = ca*cb*sc - sa*sb*cc
//   C_nb from q (eq. 1.8-16/1.8-18), checked equal to the Euler form for every case:
//     [ w2+x2-y2-z2  2(xy+wz)     2(xz-wy)    ]
//     [ 2(xy-wz)     w2-x2+y2-z2  2(yz+wx)    ]
//     [ 2(xz+wy)     2(yz-wx)     w2-x2-y2+z2 ]
//   Euler from C_nb (eq. 1.3-11, 1-based indices): psi = atan2(C12, C11), theta = -asin(C13), phi = atan2(C23, C33)
//   Kinematics (eq. 1.8-15): qdot_nb = 0.5 * q_nb (x) (0, omega_b), Hamilton product, omega_b = (p, q, r).
//     The other order, 0.5 * (0, omega_b) (x) q_nb, gives different numbers for every case except pure yaw.
// clang-format on
//
// Physical sanity of the 90-degree cases, for a reader checking signs by eye:
//   yaw_90   nose east:           north is off the left wing  -> north_in_b = (0, -1, 0)
//   pitch_90 nose straight up:    down is behind the tail     -> down_in_b  = (-1, 0, 0)
//   roll_90  right wing down:     right wing points down, belly faces west -> east_in_b = (0,0,-1)
// pitch_90 is gimbal lock (psi, phi not unique): euler_roundtrip is false, only theta is pinned.
#pragma once

namespace astra::test::adr0001 {

inline constexpr double kTolerance = 1e-12;

// NOLINTBEGIN(modernize-avoid-c-arrays): a data table; C arrays keep the braces one level deep.
struct Case {
  const char* name;
  double euler_nb[3];  // psi, theta, phi [rad] used to build the attitude
  double q_nb[4];      // w, x, y, z
  // v_b = C_nb v_n, Stevens & Lewis DCM notation.
  // NOLINTNEXTLINE(readability-identifier-naming): matches that notation, not a case-style bug.
  double C_nb[3][3];
  bool euler_roundtrip;      // false at theta = +-90 deg (only theta is checked there)
  double n_axes_in_b[3][3];  // rows: north, east, down expressed in b  (= columns of C_nb)
  double b_axes_in_n[3][3];  // rows: body x, y, z expressed in n       (= rows of C_nb)
  double omega_b[3];         // p, q, r [rad/s]
  double qdot_nb[4];         // 0.5 * q_nb (x) (0, omega_b)
};

inline constexpr Case kCases[] = {
    {"yaw_30",  // psi=30, theta=0, phi=0 deg
     {0.5235987755982988, 0.0, 0.0},
     {0.9659258262890683, 0.0, 0.0, 0.25881904510252074},
     {{0.8660254037844387, 0.49999999999999994, 0.0},
      {-0.49999999999999994, 0.8660254037844387, 0.0},
      {0.0, 0.0, 1.0}},
     true,
     {{0.8660254037844387, -0.49999999999999994, 0.0},
      {0.49999999999999994, 0.8660254037844387, 0.0},
      {0.0, 0.0, 1.0}},
     {{0.8660254037844387, 0.49999999999999994, 0.0},
      {-0.49999999999999994, 0.8660254037844387, 0.0},
      {0.0, 0.0, 1.0}},
     {0.0, 0.0, 1.0},
     {-0.12940952255126037, 0.0, 0.0, 0.48296291314453416}},
    {"pitch_30",  // psi=0, theta=30, phi=0 deg
     {0.0, 0.5235987755982988, 0.0},
     {0.9659258262890683, 0.0, 0.25881904510252074, 0.0},
     {{0.8660254037844387, 0.0, -0.49999999999999994},
      {0.0, 1.0, 0.0},
      {0.49999999999999994, 0.0, 0.8660254037844387}},
     true,
     {{0.8660254037844387, 0.0, 0.49999999999999994},
      {0.0, 1.0, 0.0},
      {-0.49999999999999994, 0.0, 0.8660254037844387}},
     {{0.8660254037844387, 0.0, -0.49999999999999994},
      {0.0, 1.0, 0.0},
      {0.49999999999999994, 0.0, 0.8660254037844387}},
     {0.0, 0.0, 1.0},
     {0.0, 0.12940952255126037, 0.0, 0.48296291314453416}},
    {"roll_30",  // psi=0, theta=0, phi=30 deg
     {0.0, 0.0, 0.5235987755982988},
     {0.9659258262890683, 0.25881904510252074, 0.0, 0.0},
     {{1.0, 0.0, 0.0},
      {0.0, 0.8660254037844387, 0.49999999999999994},
      {0.0, -0.49999999999999994, 0.8660254037844387}},
     true,
     {{1.0, 0.0, 0.0},
      {0.0, 0.8660254037844387, -0.49999999999999994},
      {0.0, 0.49999999999999994, 0.8660254037844387}},
     {{1.0, 0.0, 0.0},
      {0.0, 0.8660254037844387, 0.49999999999999994},
      {0.0, -0.49999999999999994, 0.8660254037844387}},
     {0.0, 0.0, 1.0},
     {0.0, 0.0, -0.12940952255126037, 0.48296291314453416}},
    {"yaw_90",  // psi=90, theta=0, phi=0 deg
     {1.5707963267948966, 0.0, 0.0},
     {0.7071067811865476, 0.0, 0.0, 0.7071067811865475},
     {{0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
     true,
     {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
     {{0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
     {0.0, 0.0, 1.0},
     {-0.35355339059327373, 0.0, 0.0, 0.3535533905932738}},
    {"pitch_90",  // psi=0, theta=90, phi=0 deg
     {0.0, 1.5707963267948966, 0.0},
     {0.7071067811865476, 0.0, 0.7071067811865475, 0.0},
     {{0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}},
     false,
     {{0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}},
     {{0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}},
     {0.0, 0.0, 1.0},
     {0.0, 0.35355339059327373, 0.0, 0.3535533905932738}},
    {"roll_90",  // psi=0, theta=0, phi=90 deg
     {0.0, 0.0, 1.5707963267948966},
     {0.7071067811865476, 0.7071067811865475, 0.0, 0.0},
     {{1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, -1.0, 0.0}},
     true,
     {{1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}},
     {{1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, -1.0, 0.0}},
     {0.0, 0.0, 1.0},
     {0.0, 0.0, -0.35355339059327373, 0.3535533905932738}},
    {"composite",  // psi=30, theta=20, phi=10 deg
     {0.5235987755982988, 0.3490658503988659, 0.17453292519943295},
     {0.9515485246437885, 0.03813457647485015, 0.18930785741199999, 0.2392983377447303},
     {{0.8137976813493738, 0.46984631039295416, -0.3420201433256687},
      {-0.44096961052988237, 0.8825641192593856, 0.16317591116653482},
      {0.37852230636979245, 0.01802831123629725, 0.9254165783983234}},
     true,
     {{0.8137976813493738, -0.44096961052988237, 0.37852230636979245},
      {0.46984631039295416, 0.8825641192593856, 0.01802831123629725},
      {-0.3420201433256687, 0.16317591116653482, 0.9254165783983234}},
     {{0.8137976813493738, 0.46984631039295416, -0.3420201433256687},
      {-0.44096961052988237, 0.8825641192593856, 0.16317591116653482},
      {0.37852230636979245, 0.01802831123629725, 0.9254165783983234}},
     {0.3, -0.2, 0.5},
     {-0.0466139851662101, 0.21398907682404128, -0.06879374592138185, 0.20567749490166212}},
    {"composite_neg",  // psi=-135, theta=-60, phi=170 deg
     {-2.356194490192345, -1.0471975511965976, 2.9670597283903604},
     {0.4890665421833401, 0.2898917418972026, -0.8137350405585702, 0.12088001929094468},
     {{-0.3535533905932738, -0.35355339059327384, 0.8660254037844386},
      {-0.5900268828079849, 0.8027015978320531, 0.08682408883346515},
      {-0.725856926373161, -0.48028131843521554, -0.4924038765061041}},
     true,
     {{-0.3535533905932738, -0.5900268828079849, -0.725856926373161},
      {-0.35355339059327384, 0.8027015978320531, -0.48028131843521554},
      {0.8660254037844386, 0.08682408883346515, -0.4924038765061041}},
     {{-0.3535533905932738, -0.35355339059327384, 0.8660254037844386},
      {-0.5900268828079849, 0.8027015978320531, 0.08682408883346515},
      {-0.725856926373161, -0.48028131843521554, -0.4924038765061041}},
     {0.3, -0.2, 0.5},
     {-0.1550772701631736, -0.11798577688304705, -0.10324758679899297, 0.21533771743990027}},
};

// NOLINTEND(modernize-avoid-c-arrays)

}  // namespace astra::test::adr0001
