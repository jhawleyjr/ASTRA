# M7 inventory addendum: the NESC F-16 files (P1 and P2)

Status: **written in M7 PR 6a; Joe decided every proposal and D1 to D8 on 2026-10-08 (§10).** §1 to §9
are the inventory as written before his decisions. §10 is authoritative where they differ. The M7 plan carries
dated amendments for what he changed, each pointing to §10.
Owner: Joe
Risk: Tier 0 (documents only).
Parent: [the M7 plan's Inventory section](m7-f16-nesc-oracle.md#inventory-of-the-sources-read-only-the-main-checkouts-docsrefs-2026-10-04),
row 6a of its PR table, and its "Obligations carried to PR plans".
Base: `origin/main` = `f5d31c8` (#106).

Every item below cites a file and line, a `varID` or `gtID`, or a Vol II table or page. Nothing is taken from
memory. The DAVE-ML standard (AIAA S-119) is not in `docs/refs`, so what an attribute means is cited from the
package's own Simulink realization, never from the standard.

Citation conventions:
- `F16_aero.dml:292` is line 292 of the file as unzipped. All files are under
  `docs/refs/Atmospheric_models/F16_package/` in the main checkout: `F16_S119_source/` holds the `.dml` files,
  `F16_equiv_SL_files/` the `.m` files, and `README/README.html` the README.
- "Vol II p.N" is the "Page #: N of 609" printed in the page header of `NASA-TM-2015-218675-Vol-II.pdf`.
- Case CSVs are `docs/refs/Atmospheric_checkcases/Atmos_<case>_*/Atmos_<case>_sim_0N.csv`, read at their first
  row (t = 0).
- Numbers marked *computed* come from scratch scripts that read these files read-only. Those scripts are not
  committed, and each computation is described where it is used.

## 1. The files used

**Archive.**
- `https://nescacademy.nasa.gov/src/flightsim/Models/Atmospheric_models.zip`, placed by Joe as
  `docs/refs/Atmospheric_models.zip`, SHA-256 `10e2dc267856ef37eaedbcdd16eb49a8c44b39301054de0fa78292c1c4aba3bc`.
- Its nested `F16_package.zip`, SHA-256 `20c60f615ae8e87d81c9d98b54fff45a2832840201499cbcfe3f45a60ef3e5b2`,
  is unzipped to `docs/refs/Atmospheric_models/F16_package/`.
- Both hashes were checked with `sha256sum` in this PR's session. `unzip -l` lists 6 entries in the outer
  archive and 34 in the nested one.

| File | Version (its own header) | Lines | Role |
|---|---|---|---|
| `F16_aero.dml` | Mod P, 2013-10-21 (`:9`) | 4077 | P1: aero |
| `F16_prop.dml` | "Initial version", 2012-08-07 (`:16-18`) | 930 | P1: propulsion |
| `F16_inertia.dml` | "Initial version", 2012-08-07 (`:16-18`) | 164 | P1: mass properties |
| `F16_control.dml` | Revision F, 2013-10-22 (`:10`) | 1187 | P2: 13.x's law |
| `F16_gnc.dml` | Revision C, 2013-10-22 (`:10`) | 1410 | P2: 15's and 16's law |
| `README/README.html` | version 6, 2013-10-22 (`:737`) | 2182 | Description, limits, NASA's trim |
| `F16_aero_verify.m`, `F16_prop_verify.m` | "Created 02-MAR-04 by DAVE2SL" (`:4`) | | Checked as a second source (§7) |
| `F16_aero_create.m`, `F16_prop_create.m`, `F16_control_create.m`, `F16_gnc_create.m` | DAVE2SL output | | Simulink realization, used for semantics only |

Each file's full SHA-256 is in §1.1.

### 1.1 Full SHA-256 of each file used

```
82796577b989b5f3becb7d13430ff6c619b3ecd2e642a6427b743658fb177e51  F16_S119_source/F16_aero.dml
bca7789dde61b741de69f697ce8799ee666214d0e1fa203973523774fc3a969b  F16_S119_source/F16_control.dml
fc1dbff00cc14b0228973c43cfc17797fd48d2e36950ec039ff4e77d8577827c  F16_S119_source/F16_gnc.dml
e11d4eba5b158d24db7034bd85682ad0b896dd2f3172f007ca3d6d6d4e693c4a  F16_S119_source/F16_inertia.dml
e5fd7772e5c05cdac666c94febbe4cb50139418b26045affd92dbd198f2215c6  F16_S119_source/F16_prop.dml
5496aa1314374d5d25f7c9f412351222eaee2f51b0fda6beebd348ace2483269  README/README.html
2a1a9421112dc88531f5fb8f537389df788f90778a3ee3a0f010b8b5e419185c  F16_equiv_SL_files/F16_aero_verify.m
45f6a360f3fba5abf4732915987d59be6e09a6dc05e3c7c1ef2e0f74c28662bb  F16_equiv_SL_files/F16_prop_verify.m
a9d61da683ddef2519f305297aa0cb13956d438afddda99a57f1875393e7c2c4  F16_equiv_SL_files/F16_aero_create.m
04170c14bf7ca6672814ebb46673bd1aa0e77257bd13b71b48ad819b413d3763  F16_equiv_SL_files/F16_prop_create.m
6079bd81da915efb94928dd558b1a2cf0b7b12e035d988a42c8fc24e4aff310f  F16_equiv_SL_files/F16_control_create.m
bbd83b20a7da32e2263dc78e28c280b49a922aeda379e814a5a339887599419e  F16_equiv_SL_files/F16_gnc_create.m
```

**Not used by M7.**
- The archive's other entries: `brick_*.dml`, `cannonball_*.dml`, and `two-stage_package.zip` (case 17, a
  non-goal).
- The `*_setup.m` files and the `README/Figures/*.png` plots.

**The README's version list is stale.** It names `F16_aero.dml` "mod O, dated 2013-09-11", `F16_control.dml`
"rev E" and `F16_gnc.dml` "rev B" (`README.html:927,942,948`). The files are Mod P, Rev F and Rev C. The
differences are these:
- Mod P changed only checkcase internal values (`F16_aero.dml:179-186`).
- Rev F and Rev C added the OR of the SAS and autopilot flags (`F16_control.dml:87-93`, `F16_gnc.dml:64-69`).
- Rev E and Rev B, which the README names, already carried the current LQR gains (`F16_control.dml:78-85`).

Which revisions SIM 4 and SIM 5 used is not stated in any file read here (D7).

## 2. Aero (`F16_aero.dml`)

The header is "F-16 Subsonic Aerodynamics Model (a la Garza)" (`:6`), a MathML port of Morelli's `f16_aero.m`.
That script is quoted in a comment (`:190-287`).

**Inputs** (`:292-349`). There is no Mach, altitude or density input.

| varID | Name | Units | Sign (file) |
|---|---|---|---|
| `vt` | trueAirspeed | ft_s, `minValue="0.1"` (`:292`; Mod L, `:140-147`: "to avoid divide-by-zero") | |
| `alpha` | angleOfAttack | deg | |
| `beta` | angleOfSideslip | deg | "wind in right ear" (`:305`); README Table 1: +ANL |
| `p`, `q`, `r` | bodyAngularRate_Roll/Pitch/Yaw | rad_s | RWD, nose up, nose right. The description is only "Body roll rate, rad/sec" (`:313`), with no reference frame (D3) |
| `el` | elevatorDeflection | deg | trailing edge down, "(+TED)" (`:332-334`) |
| `ail` | aileronDeflection | deg | "left roll", "(+LWD)" (`:339-340`) |
| `rdr` | rudderDeflection | deg | TEL, "(+TEL)" (`:345-346`) |

**Outputs.**
- cbar = 11.32 ft, bspan = 30 ft and sref = 300 ft² (`:368-384`).
- Six coefficients (`:784-946`): cx (+FWD), cy (+RIGHT), cz (+DOWN), cl (+RWD), cm (+ANU) and cn (+ANR).
- They act "at or about the moment reference center" (Mod O, `:167-176`: "moment transfers to the center of
  mass must be done externally to this routine"; D4).
- The model returns coefficients only. Dimensionalizing them with q̄, S, b and c̄ is the user's job.

**Breakpoints** (`:952-970`), all in deg:

| bpID | Points | Values |
|---|---|---|
| ALPHA1 | 12 | −10, −5, 0, 5, …, 45 |
| BETA1 | 7 | 0, 5, …, 30 (used against \|β\|) |
| BETA2 | 7 | −30, −20, …, 30 |
| DE1 | 5 | −24, −12, 0, 12, 24 |

**Tables.** There are 18 `griddedTableDef`s, each inside its own `function`. The *computed* value count
matches the product of each table's breakpoints in every case.

| Function (line) | gtID | Breakpoints (slowest first) | Values | Output |
|---|---|---|---|---|
| Basic CX (`:976`) | CX_table_def | DE1 × ALPHA1 | 60 | `cxt` |
| Basic CZ (`:1011`) | CZ0_table_def | ALPHA1 | 12 | `czt` |
| Basic Cm (`:1037`) | Cm0_table_def | DE1 × ALPHA1 | 60 | `cmt` |
| Basic Cl (`:1077`) | Cl0_table_def | BETA1(\|β\|) × ALPHA1 | 84 | `absCl0` |
| Basic Cn (`:1113`) | Cn0_table_def | BETA1(\|β\|) × ALPHA1 | 84 | `absCn0` |
| CXq, CYr, CYp, CZq, Clr, Clp, Cmq, Cnr, Cnp (`:1159-1370`) | `*_table_def` | ALPHA1 | 9 × 12 | `cxq` … `cnp` |
| dlda, dldr, dnda, dndr (`:1400-1522`) | `*_table_def` | BETA2 × ALPHA1 | 4 × 84 | `dclda` … `dcndr` |

That is 744 values in all. Each table states "last breakpoint changes most rapidly" (for example `:1000`).

**Extrapolation.**
- Every `independentVarRef` in the file carries `extrapolate="neither"`, with `min` and `max` equal to its
  breakpoints' ends (for example `:987-989`).
- The package's Simulink realization implements every aero table as `Interpolation_n-D` with
  `'ExtrapMethod','clip'` (`F16_aero_create.m:95-123`). Its prelookups say `'Linear'` with no out-of-range
  diagnostic (`:94`, `:125`).
- So the file's own realization reads "neither" as **clip**: past an edge, a table holds its edge value.

**The build-up** (MathML `calculation`s, `:355-946`). It uses `plus`, `minus`, `times`, `divide`, `power`,
`abs`, and `piecewise` with `lt`. It uses no `csymbol`. In the file's units:
- `rtd = 180/3.14159265` (`:355-366`), a truncated π, used only in cz1's `(beta/rtd)^2`.
- `del = el/25`, `dail = ail/20`, `drdr = rdr/30` (`:390-429`). Of `del`, the file says "note range is beyond
  breakpoint range" (`:392`).
- `cy0 = −0.02·beta + 0.021·dail + 0.086·drdr` (`:441-469`).
- `cz1 = czt·(1 − (beta/rtd)²) − 0.19·del` (`:479-532`).
- `tvt = 2·vt`, `b2v = bspan/tvt`, `cq2v = cbar·q/tvt` (`:536-580`).
- `clt` and `cnt` are ±`absCl0` and ±`absCn0`, negative when `beta < 0` (`:607-663`).
- `cl1 = clt + dclda·dail + dcldr·drdr` and `cn1 = cnt + dcnda·dail + dcndr·drdr` (`:734-778`).
- The outputs (`:784-946`):
  - `cx = cxt + cq2v·cxq`;
  - `cy = cy0 + b2v·(cyp·p + cyr·r)`;
  - `cz = cz1 + cq2v·czq`;
  - `cl = cl1 + b2v·(clp·p + clr·r)`;
  - `cm = cmt + cq2v·cmq`;
  - `cn = cn1 + b2v·(cnp·p + cnr·r)`.
- The comment at `:911` ("cn + b2v*() - cy*()*cbar/bspan") is stale. The MathML has no cy term, which Mod O
  removed (`:170-176`).

**The rate terms** (Q7, amendment 2). The rates enter only through nine damping derivatives:
- the forces: cxq and czq per c̄q/2V, and cyp and cyr per b/2V;
- the moments: clp, clr, cmq, cnp and cnr, per b/2V or c̄q/2V.
The derivatives are functions of α alone. No other term reads p, q or r.

**State.** The model binds none. It is a pure function of its nine inputs: no `isState` or `isStateDeriv`
variable, and no time or memory element (the *computed* survey of `variableDef` flags).

**checkData** (`:1563-4076`).
- 16 `staticShot`s (`:1564` "Nominal" … `:3919` "Skewed inputs"), each with 9 outputs (the 3 reference
  lengths and the 6 coefficients).
- Every output's `tol` is `0.000001` (144 of 144, *computed*).
- Every shot has vt = 300 ft/s.
- α ∈ {5, 16.2}°, β ∈ {0, ±2.34, −3.24}°, and the controls lie within the breakpoints. **No shot lies beyond
  a table edge**, so checkData does not exercise the extrapolation.
- All 16 shots carry `internalValues` (*computed*).
- "Positive aileron" (`:3291`), ail = +24.1°, gives cl = −0.06266 (`:3431`), a left-wing-down roll. The
  physical sign of `ail` is +LWD as labeled (D2).

## 3. Propulsion (`F16_prop.dml`)

**Inputs** (`:44-65`):
- `PWR`: powerLeverAngle, pct, "0 - 100. 50 is MIL (max dry) thrust; 100 is MAX (burner)";
- `ALT`: altitudeMSL, ft (README Table 3: "Geometric altitude above reference geoid", `README.html:1161`);
- `RMACH`: mach, nd.

**Breakpoints** (`:228-244`): ALT_PTS is 0 to 50,000 ft in 10,000 ft steps (6 points), and MACH_PTS is 0 to 1.0
in 0.2 steps (6 points).

**Tables** (`:257-327`): T_IDLE, T_MIL and T_MAX, each MACH_PTS × ALT_PTS, 36 values (108 in all). The
description of T_MAX at `:307` says "Idle thrust table", a copy-paste slip; its function is "Maximum thrust
table lookup function" (`:359`).

**Extrapolation.**
- Every `independentVarRef` is `extrapolate="neither"`, with min and max at the breakpoint ends (`:337-367`).
- The Simulink realization is `'Linear','None - Clip'` (`F16_prop_create.m:49,52,55`), so the tables clip.
- **There is nothing beyond Mach 1.0 or 50,000 ft.**

**Build-up** (`:107-177`). `FEX = T_IDLE + PWR·(T_MIL − T_IDLE)/MIL_PWR` for `PWR < MIL_PWR`, and otherwise
`T_MIL + (PWR − MIL_PWR)·(T_MAX − T_MIL)/(100 − MIL_PWR)`, with `MIL_PWR = 50` (`:72-76`).

**Outputs.**
- FEY, FEZ, TEL, TEM and TEN are constant `initialValue="0.0"` (`:179-222`).
- The thrust is "Net steady-state thrust" (`:109`).
- README Table 4 says FEY, FEZ and the three moments are "always 0" (`README.html`, Table 4).

**Dynamics.**
- There is no engine state, no power lag, and no rotor angular momentum, so **no gyroscopic term**.
- No `variableDef` is a state, and no input is a rate.
- The engine is a static map of (PWR, ALT, Mach).

**checkData** (`:369-928`).
- 9 shots, each with 6 outputs.
- `tol` is 1e-5, except FEX at `0.001` (`:822`) and `0.0006` (`:894`).
- The shots sit at the envelope's corners (Mach 0 and 1, 0 and 50,000 ft) and two interior points. None lies
  beyond an edge.

## 4. Mass properties (`F16_inertia.dml`)

- Constants (`:56-135`): Ixx 9,496.0, Iyy 55,814.0, Izz 63,100.0, Izx 982.0 ("Cross-product of inertia in the
  body X-Z plane"), and Ixy = Iyz = 0, all in slug·ft².
- **XMASS = 637.1595 slug**, "Total mass of vehicle (20,500 lbm)" (`:111-117`; D1).
- README Table 6 says the products are "normal" with "no sign reversal".
- Input `CG_PCT_MAC` (`:44-49`) defaults to `initialValue="35.0"` and is +AFT. The README says "the input
  value for the CG position should be 25%" (`README.html:1234`; also `:1260`, `:1824`, and v5 of its change
  history at `:2123`). Vol II Table 7 has x̄ = 25 % MAC (Vol II p.15).
- `DXCG = 0.01·CBAR·(35 − CG_PCT_MAC)` ft, +FWD (`:141-162`). At 25 % that is **1.132 ft, CM forward of the
  MRC**. DYCG = DZCG = 0.
- There is no checkData.

## 5. The control laws (`F16_control.dml`, `F16_gnc.dml`)

**Shape.**
- Both laws are memoryless. Every `variableDef` is an input, a constant, or a `calculation` of the same
  frame's values: no state, integrator, filter or delay (the *computed* MathML listing).
- Vol II p.16 calls `F16_control.dml` "the normative description" (p.26 says the same of `F16_gnc.dml`). The README
  says "the control system has no integrated states" (`README.html`, "A note on initial closed-loop
  responses").
- **Neither file states an execution rate.** No `variableDef` or comment names a sample time.
- The Simulink realizations set no `SampleTime` on any block (*computed*: zero matches in each
  `*_create.m`).

**Inputs** (`F16_control.dml:100-231`; `F16_gnc.dml:77-215`):

| varID | Name | Units | Sign | Note |
|---|---|---|---|---|
| `throttle` | pilotControl_throttle | frac | | `minValue 0`, `maxValue 1` |
| `longStk`, `latStk`, `pedal` | pilotControl_long/lat/yaw | frac | +AFT, +RWD, +ANR | each in [−1, 1] |
| `sasOn`, `apOn` | stabilityAugmentationOn_disc, autopilotOn_disc | nd | | true when > 0.5 |
| `keasCmd` | equivalentAirspeedCommand | nmi_h (KEAS) | | |
| `altCmd` | altitudeMslCommand | ft | | |
| `latOffset` | lateralDeviationError | ft | +RT | control only (`:152`) |
| `baseChiCmd` | trueBaseCourseCommand | deg | +CCFN in the file, +CWFN in README Table 7 | control only (`:157`) |
| `altMsl` | altitudeMsl | ft | | |
| `Vequiv` | equivalentAirspeed | nmi_h (KEAS) | | |
| `alpha`, `beta` | angleOfAttack, angleOfSideslip | deg | β "+RT" (`:182`), README +ANL | |
| `phi`, `theta`, `psi` | eulerAngle_Roll/Pitch/Yaw | deg | +RWD, +ANU, +CWFN | |
| `pb`, `qb`, `rb` | bodyAngularRate_* | rad_s | +RWD, +ANU, +ANR | The same S-119 names as the aero's inputs. No frame is stated (D3) |
| `throttleTrim`, `longStkTrim` | trimmedPilotControl_throttle/long | frac | +AFT | Inputs since Mod D (`:68-75`). Defaults 0.1390191130965607 and 0.1296382327486013 (`:226-235`) |
| `circlePoleSW` | selectCircumnavigator_disc | nd | | gnc only (`F16_gnc.dml:117`) |
| `ownshipN_deg`, `ownshipE_deg` | geLatitude, geLongitude | deg | +N, +E | gnc only, "Geodetic … of vehicle CM" (`F16_gnc.dml:124-133`) |

**The law** (control; gnc is identical from `chiEst` on). Line numbers are in `F16_control.dml`.
- **Engagement.** `fsasOn = sasOn + apOn` (`:242`), true when > 0.5. This is Rev F's "OR".
- **Altitude.** `altErr = altMsl − altCmd`. Then `deltaThetaCmd = −0.05 deg/ft · altErr`, limited to [−5, 5]°
  (`:260-300`), and `thetaCmd = deltaThetaCmd + trimmedTheta` (`:301`).
- **Course.**
  - `chiEst = beta + psi` (`:319`).
  - `deltaChiCmd = −0.01 deg/ft · latOffset`, limited to [−30, 30]° (`:335-369`), and
    `chiCmd = deltaChiCmd + baseChiCmd`.
  - `chiErr` is `chiEst − chiCmd`, wrapped into ±180° (`:385-452`).
  - `phiCmd = −10 deg/deg · chiErr`, limited to [−30, 30]° (`:344`, `:453-470`).
- **LQR references** (`:471-487`). trimmedAlpha = trimmedTheta = 2.653813535191715°, and trimmedKEAS =
  287.8088596053291, whose units are written `nim_h`, a typo for `nmi_h` (`:481`). The switch blocks
  (`:488-566`) use the commands when `apOn > 0.5`, and otherwise trimmedKEAS, trimmedTheta and φ = 0.
- **LQR gains** (`:571-655`):
  - long: `−0.063009074230494` (h_nmi), `0.113230403179271` (_deg), `10.113432224566077` (s_r) and
    `3.154983341632913` (_deg);
  - throttle: `0.997260602961658`, `−0.025467711176391`, `1.213308488207827` and `0.208744369535208` (same
    units);
  - lat: `3.078043941515770` (_deg), `0.032365863044163` (_deg), `4.557858908828332` (s_r) and
    `0.589443156647647` (s_r);
  - dir: `−0.705817452754520`, `−0.256362860634868`, `−1.073666149713151` and `0.822114635953878`;
  - each LQR output is the negated sum, gain times (ΔV, Δα, q, Δθ) or (Δφ, β, p, r) (`:714-853`).
  - Vol II Figure 7 (p.23), rendered from the PDF, prints all 16 gains with the same digits (D8). Mod E's
    gains are therefore the ones Vol II documents.
- **The engage and limit-and-scale block** (`:854-1171`):
  - the LQR terms pass when `fsasOn > 0.5`, and the pilot terms when `apOn < 0.5`;
  - `totLongStk = longStkTrim + longStkSw + longLQRsw` and `totLatStk`/`totPedal`, each limited to [−1, 1]
    (`minValue`/`maxValue`, `:1042-1093`);
  - `totThrottle = throttleTrim + throttleSw + throttleLQRsw`, limited to [0, 1] (`:1094-1110`);
  - **the outputs**:
    - `el = −25·totLongStk` (deg, +TED);
    - `ail = −21.5·totLatStk` (deg, +LWD);
    - `rdr = −30·totPedal + 0.008·ail` (deg, +TEL; the interconnect reads the **+LWD** `ail`);
    - `PWR = 100·totThrottle` (pct) (`:1117-1185`).
  - The Simulink realizes each `minValue`/`maxValue` as a `Saturate` block, inputs and totals alike
    (`F16_control_create.m:35-143`, `F16_gnc_create.m:36-188`).
  - The elevator's ±25° exceeds the aero's ±24° DE1, which clips (§2).
- **The inverse for trim** (R23). With SAS and autopilot off and the pilot stick at zero (Vol II p.16: "No
  pilot inputs were used"), `el = −25·longStkTrim` and `PWR = 100·throttleTrim`. NASA's own trim, README
  Table 11 (`README.html:1855-1890`), is consistent: −25 × 0.1296382327 = −3.2410°, the "Horizontal tail
  position", and 13.9019 % throttle ↔ PLA 13.9019.

**The GNC navigator** (`F16_gnc.dml:300-540`):
- `deg_to_ft = 60.0·6076.12` (60 nm per degree, 6076.12 ft per nm; `:303`) and
  `circleRadius_ft = 3.0·6076.12` (`:317`).
- Polar: `latOffsetPolar = deg_to_ft·(90 − lat) − radius` (+RT) and `baseChiCmdPolar = 90` (`:332-358`).
- Equator and IDL:
  - `degEFromTgt` is the longitude relative to the IDL (`:360-391`);
  - `ownshipE_ft` uses `cos(lat·3.14159265/180)`, another truncated π (`:405-430`);
  - `ftFromTgt = sqrt(E² + N²)`;
  - `baseChiCmdEquatorIDL = atan2(ownshipN_ft, ownshipE_ft)·(−180)/3.14159265` (`:456-477`);
  - `latOffsetEquatorIDL = ftFromTgt − radius` (`:478`).
- **The `atan2` order.** The MathML's arguments are (N, E), but the comment beside them says
  `atan2(E, N)` (`:469-473`). The Simulink wires `ftNFromTgt` to input 1 and `ftEFromTgt` to input 2
  (`F16_gnc_create.m:379-386`), which is (N, E), the MathML's order. The geometry agrees: a counterclockwise
  circle's course at a point due east of the center is north, and −atan2(0, +E) = 0. The comment is wrong.

## 6. Units, per Q26

The laws' own units:
- **Inputs:** ft, KEAS (`nmi_h`), deg, rad/s, frac and nd.
- **Outputs:** deg and pct.
- **Gains:** deg_ft (−0.05, −0.01), deg_deg (−10), and the LQR's `h_nmi`, `_deg` and `s_r`.
- **Limits:** ±5° and ±30° (twice). The sticks lie in [−1, 1] and the throttle in [0, 1].
- **Mixer scales:** −25, −21.5 and −30 deg per frac, 0.008 deg/deg, and 100 pct per frac.
- **Numeric constants inside the laws:** π as `3.14159265` (`F16_gnc.dml:405-477`), 6076.12 ft/nm and
  60 nm/deg (`:303-317`). The aero has its own `rtd = 180/3.14159265` (`F16_aero.dml:355`).

**ICD v1's side** (`icd/astra_icd.h`):
- φ, θ, ψ, α, β, lat and lon are in rad, and tas, cas and altitude in SI.
- p, q and r are inertial body rates.
- The aileron is positive right wing down, the elevator +TED and the rudder +TEL (TEL being "nose-left
  yawing moment").
- The throttle is `throttle[2]` in 0..1.
- So the elevator and rudder conventions match the DML, and the aileron's is opposite.

## 7. Second check sources

- **`F16_aero_verify.m` and `F16_prop_verify.m`.**
  - The DAVE2SL utility generated them ("Created 02-MAR-04", `:4`).
  - A *computed* comparison finds them **identical to the DML's checkData**: 16 of 16 and 9 of 9 shots, with
    the same inputs, outputs and tolerances (largest difference 0).
  - So they are copies, not an independent oracle.
  - They do fix the meaning of `tol`: a shot passes when `abs(y−y_good) < y_tol` for every output, an
    absolute and strict test (`F16_aero_verify.m:686`, `F16_prop_verify.m:291`).
- **The `*_create.m` Simulink realizations.** These are generated too. They are used here only to settle
  semantics: the clip extrapolation, the `atan2` wiring and the saturations.
- **README Table 11.**
  - It is NASA's own trim at 10,013 ft and 565.6854 ft/s, CM 25 %: pitch 2.6538°, stick 12.96 % aft, tail
    −3.2410°, throttle and PLA 13.9019 % (`README.html:1855-1890`).
  - Its simulation used "an oblate rotating spheroidal Earth with a constant 32.174 ft/s^2 gravity field"
    (`:1823`), so it is a cross-check of R23's inverse mapping, not an oracle for ASTRA's J2 trim.
- **No checkData in the laws or the inertia file.** The README states that only the aero and propulsion files
  carry checkcases (`README.html:1812`). R11's reference evaluation stays ASTRA's own independent one, as the
  plan has it.
- **P3** (Garza & Morelli, TP-1538) is not needed for R2 or R3.

## 8. Proposals (decided 2026-10-08; see §10)

- **Q5's condition** (critique C-07, amendment 6). The condition holds on the files.
  - The aero binds no own or peer state: it is a pure function of 9 inputs (§2).
  - The propulsion binds none either, being a static map of 3 inputs with no power lag (§3).
  - So (c), the independent aero evaluator, is valid.
  - Amendment 6's self-check needs no engine state.
  - PR 5b is not scheduled, since no model owns continuous state.
  - One sub-condition: **the rate the aero model uses comes from the sample's state, its environment, or the
    model's own constants, never from a peer binding.** This does not pre-decide D3's design.
- **Q12, the law's divisor.** The DMLs state no execution rate (§5), so Joe's decided fallback applies:
  **exchange divisor 10 (100 Hz).**
  - The laws are memoryless, so the rate acts only through how long each command is held.
  - Context only, not a source for the divisor. The participating tools' rates for the F-16:
    - LaSRS++ ran cases 11 and 13 to 16 at 100 Hz and case 12 at 200 Hz (Table 21, p.51: "supersonic
      vehicle dynamics not stable at 100 Hz");
    - POST II ran the atmospheric cases at 100 Hz (Table 22, p.52);
    - VMSRTE and Core used 0.01 s (B.6.5, p.52; B.6.6, pp.52-53);
    - JSBSim used 120 Hz (B.6.7, p.53).
  - Which tool is SIM 4 or SIM 5 is masked (p.92).
- **Q14, the aero tables.** The plan's recommendation stands unchanged.
  - A stdlib `xml.etree` generator emits committed tables with each source file's SHA-256 (§1.1).
  - The build-up is hand-ported.
  - Plugin-local interpolation implements "neither" as clip.
  - A separate reader extracts checkData.
  - The size is 852 table values (744 aero plus 108 propulsion), not "thousands".
  - R2's and R3's oracle is each file's checkData at its stated absolute `tol` (§2, §3), so P3 is not needed.
  - No checkData shot lies beyond an edge, so the clip behavior at each table's edges needs its own test,
    pinned against the Simulink realization's clip.
- **Q15, case 12 and the table edges** (final text proposed).
  - The F-16 aero has no Mach or altitude dependence (§2).
  - The propulsion tables stop at Mach 1.0 and 50,000 ft and clip (§3).
  - Case 12 is at Mach 2.0104687 (SIM 5's CSV, t = 0) and 30,013 ft. It is flown with the subsonic aero
    tables and the Mach-1.0 thrust row, which is what the DML specifies for every participant.
  - The plugins reproduce the clip at every edge.
  - Whether case 12 trips an envelope counter is D6.
- **Q26, the SI realization's details.**
  - Every gain and limit is written as the DML's value times named `core/util/units` conversions, as decided.
    In the port it is written with the port's own named constants.
  - **The DML's own numeric constants stay, as named constants, with their effect kept:** π = 3.14159265
    inside `cos` and `atan2`, 6076.12 ft/nm, and 60 nm/deg.
    - Converting `lat` exactly but evaluating `cos(lat)` instead of the DML's `cos(lat·3.14159265/180)`
      changes the GNC's east distance by a relative amount of about tan(lat)·lat·1.1e-9.
    - At 89.95° that is about 2.1e-6 (*computed*: 1.14e-9 × 1.570 rad × tan 89.95° ≈ 1146).
    - That is far above R11's 1e-12. The truncated π is therefore part of the law, not a unit conversion.
  - The aileron-to-rudder interconnect (`0.008·ail`) reads the DML's +LWD aileron before the law's boundary
    flips it to ICD v1's +RWD.
  - Every DAVE-ML `minValue`/`maxValue` is a saturation, inputs included (§5).
  - KEAS is converted with the exact knot, 1852/3600 m/s, which the files do not state. The GNC's 6076.12
    ft/nm is not that exact value; it stays a DML constant inside the navigator only.

## 9. The differences brought to Joe (D1 to D8; decided 2026-10-08; see §10)

Each records both sides. None is chosen here.

### D1. The mass

- `F16_inertia.dml:111` has **637.1595 slug** ("20,500 lbm"). Vol II Table 7 (p.15) has **637.26 slug**, and
  R1 is written against Table 7.
- The difference is 0.1005 slug. At case 11's local gravity, 32.1886 ft/s² (SIM 4's CSV), it is **3.24 lbf
  (14.4 N) of weight**. Case 11's generated force_z band is **0.4766 N**
  (`tests/golden/nesc/case11_trim_check_subsonic_f16_band.hpp`, `kForceZ`), and trim must carry the extra
  weight in lift.
- So the choice is visible in case 11's band. Which value SIM 4 and SIM 5 used is not stated in any file read
  here.

### D2. The aileron sign

- The aero input (`F16_aero.dml:339-340`), the laws' output (`F16_control.dml:1133`), README Tables 1, 8 and 10,
  and Vol II Table 9 (p.20) all say +LWD.
- checkData confirms the physical sign: ail = +24.1° gives cl = −0.06266 (`F16_aero.dml:3431`).
- ICD v1 is +RWD. **No disagreement:** the plan's flip, at the law's boundary and inside the aero plugin, is
  confirmed.
- One added fact: the rudder interconnect reads the +LWD aileron (§5).

### D3. The body-rate frame (a framework question, decided by Joe before PR 6b's plan)

**What the files say.**
- The aero's `bodyAngularRate_*` are "Body roll rate, rad/sec" and so on (`F16_aero.dml:311-330`), with no
  frame.
- The laws use the same names (`F16_control.dml:206-223`). README v2 records only "Corrected units on angular
  rate inputs" (`README.html:2108`).

**What Vol II and the members' data say about the aero's rates.**
- Vol II D.1.17 (case 15), p.309, verbatim: "In SIM 4, the zero rotational rate relative to the Earth induced
  no aerodynamic side force, rolling moment, or yawing moment." The passage is about case 15, where SIM 4 "appeared
  to have modified its equilibrium solution to target an angular rate of zero in the Earth-relative frame"
  (same page).
- In case 11, SIM 4 held a pitch rate instead: Vol II D.1.11, p.228, says "SIM 4 solved for the pitch rate that
  maintained the pitch angle as the vehicle flew over the curved surface of the Earth". The same passage gives
  SIM 5's "equilibrium roll and yaw rate" as "3 × 10−5 and 8 × 10−4 deg/s", which are its Earth-relative
  values below. Its inertial values are 2.5e-3 and −3.1e-3 deg/s.
- D.1.17's same paragraph, of SIM 2: "a zero inertial rate translated into an Earth-relative rate where much of the
  Earth's rotation rate appeared in body-axis yaw rate rb … The Earth-relative rate induced an aerodynamic side
  force, rolling moment, and yawing moment".
- Case 11's first rows (t = 0), with the rates being the CSVs' inertial `bodyAngularRateWrtEi` columns:

  | Member | p, q, r (deg/s) | Aero Y (lbf) | Aero L (lbf·ft) | Aero N (lbf·ft) |
  |---|---|---|---|---|
  | SIM 4 | 2.50011334537e-3, −3.94714404607e-3, −2.3443260021e-3 | 4.44998745201e-11 | −7.01484256089e-16 | 1.5633486196e-15 |
  | SIM 5 | 2.533320382709163e-3, −3.939291659912435e-3, −3.138617072930523e-3 | −2.8504245996912474e-2 | −9.976370046456123e-2 | 3.8802760616272963e-1 |

- **A *computed* check.**
  - Method: subtract ω_ie in body axes from each member's inertial rate. That uses Vol II Table 73's
    ω = 7.292115e-5 rad/s at the CSV's latitude, heading and pitch. Then evaluate the DML's damping tables at
    the CSV's α (= θ), V and q̄. With β, ail and rdr all zero at trim, the rate terms are the whole lateral aero.
    Finally apply the transfer to the CM (D4).
  - SIM 4's Earth-relative p and r come out at 3e-13 and −1e-12 deg/s: its inertial p and r are exactly the
    Earth rate's components. Its Earth-relative q is **not** zero: −1.5576e-3 deg/s, from an inertial
    −3.9471e-3 less the Earth rate's −2.3895e-3. That is the transport pitch rate D.1.11 describes, and it
    enters CXq, CZq and Cmq.
  - SIM 5's Earth-relative rates are (3.320e-5, −1.5498e-3, −7.943e-4) deg/s.
  - **Earth-relative rates predict SIM 5's Y, L and N** as −2.850424672e-2, −9.976370299e-2 and
    3.880276160e-1, **matching its CSV to 3e-8 relative**.
  - Inertial rates predict −0.1155, −1.6046 and 1.5877, which miss by factors of 4 to 16.
  - SIM 4's recorded values, about 1e-11 to 1e-15, also fit only Earth-relative rates. Inertial rates would
    give −0.087 lbf, −1.505 lbf·ft and +1.200 lbf·ft.
- **The members fed the aero the body rate relative to the Earth**, which with no wind is the rate relative to
  the air. In case 11 that rate is zero in roll and yaw for SIM 4, and nonzero in pitch for both members.
- This contradicts the M7 plan's Assumption 3 (L95) for the aero: "The DML's `bodyAngularRate_*` inputs are
  the same rates" as the inertial ones.

**What the laws' rates are: silent.** No file read here, and no Vol II passage found, states which rate SIM 4
and SIM 5 fed `pb`, `qb` and `rb`.

**Every reader of body rates in the tree today, and what it receives.** Each receives the inertial ω_b
(`VehicleState::omega_b`, `core/include/astra/kernel/vehicle_state.hpp:38`; ADR-0001, ADR-0003). The
`EnvironmentSample` a `wrench_b` receives carries only `wind_n` and `atmosphere`
(`core/include/astra/kernel/environment_sample.hpp`).

| Reader | What it does with the rate | Earth model in its configs |
|---|---|---|
| `aircraft/notional_transport/physical/aero/aero.cpp:94` (`in.omega_b = vs.omega_b()`), through `aero_math.hpp:126-147` | roll, pitch and yaw damping | `wgs84_rotating`: `cruise.yaml`, `cruise_still_air.yaml`, `cruise_{light,moderate}_turbulence.yaml` |
| `aircraft/_template/physical/aero/aero.cpp:96`, with an identical `aero_math.hpp` | the same, copied by `just new-aircraft` | the template's `cruise.yaml` uses `earth/flat` (the §18 step 4 item) |
| The F-16's aero (PR 6b, to come) | the nine damping derivatives (§2) | `wgs84_rotating` (cases 11 to 16) |
| `tests/plugins/nesc_aero/nesc_aero.cpp:88-90` (M2's brick damping) | its header (`:18-20`) states deliberately that the rates are inertial | `wgs84_rotating` (`tests/physics/nesc/case03_tumbling_brick_damping.yaml:25`) |
| The core's EOM, `core/src/kernel/rigid_body_6dof_dynamics.cpp:28-35` | already forms `omega_ie_b` and `omega_en_b` from the Earth model, for the attitude kinematics | all |
| `core/include/astra/util/icd_pack.hpp:88-90`, read by M4's `embedded/autopilot/autopilot.cpp:63,72,75` | packs the inertial ω_b into ICD v1's `p, q, r`, which the M4 autopilot's rate feedback reads | all |

**The design options for PR 6b** (none chosen):

- **(A) The plugin computes the Earth-relative rate itself,** from the sample's state and its own constants:
  ω_b − C_bn·ω_ie^n(lat).
  - It needs the latitude, which lives in the Earth model's own position representation (ADR-0021), and an
    Earth rate the plugin carries itself.
  - The plugin cannot see whether the configured Earth model rotates (`earth/round` defaults to non-rotating),
    so its constant can disagree with the Earth model. That is the kind of inconsistency PR 3's
    rotation-consistency check refuses between the Earth and gravity models.
  - *Blast radius:* the F-16 plugin only. The notional transport is unchanged; M3's frozen regression
    (`tests/unit/kernel/notional_transport_cruise_regression_test.cpp`), the R1 golden
    (`tests/golden/dynamics/cruise_end_to_end_r1_golden.hpp`), M4's lockstep values
    (`tests/python/test_lockstep.py`, the `cruise*.yaml` configs) and M6's values (`mc_planted_fixtures`, from
    `cruise.yaml`) are all unchanged. There is no core change, ADR or `kCoreApiVersion` bump.
- **(B) The core supplies a body rate relative to the Earth or the air**, in `EnvironmentSample` or through an
  `air_data` helper fed by the Earth model the core already queries. This needs an ADR (§7, ADR-0020's seam)
  and a `kCoreApiVersion` bump, since `EnvironmentSample` crosses the plugin ABI.
  - *(B1) Only the F-16 reads it;* the notional transport, the template and `nesc_aero` keep the inertial ω_b.
    *Blast radius:* the core, the ADR and the bump, with every plugin rebuilt. No existing value moves: M3,
    the R1 golden, M4 and M6 are unchanged, and M2's case 3 keeps its band. The tree then carries two
    documented conventions.
  - *(B2) Every aero migrates*, the notional transport and the template included. *Blast radius:* every
    notional-transport config runs `wgs84_rotating` (table above), so these values move:
    - M3's frozen regression;
    - the R1 golden, a guarded golden needing Joe's `touch-goldens`;
    - M4's lockstep acceptance;
    - M6's R9 batch and its negative control;
    - `test_cruise_phugoid` and `test_autopilot_design`.
    The size is unmeasured. If `nesc_aero` migrates too, M2's case 3 moves, against the invariant that
    cases 1 to 10 keep their bands.
- Either way, **the laws' rates are a separate choice.** One option keeps the ICD's inertial p, q and r, as
  M4's autopilot does and as the plan assumes. The other has the peer subtract ω_ie, from the ICD's lat and
  Euler angles, before the law. The files do not decide it.

**The stakes for case 11** (amendment 2; an estimate, not a gate). Suppose the aero damped the inertial rate
while the members damped the Earth-relative one. The extra damping is then the aero's response to ω_ie^b,
which is constant over the run.

*Computed* with a linear lateral-directional model (β, p, r, φ) about SIM 4's t = 0 trim. Assumptions, all
stated:
- **Derivatives from `F16_aero.dml` at α = 2.6387°.** The derivative values:
  - clp −0.4309, clr 0.0894, cnp 0.0182, cnr −0.3822, cyp −0.0307, cyr 0.9193;
  - Clβ and Cnβ are the Cl0 and Cn0 slopes between |β| = 0 and 5°: −0.1159 and 0.2123 per rad;
  - CYβ = −0.02 per deg.
- **Flight condition:** V = 565.70 ft/s and q̄ = 280.79 psf from SIM 4's CSV; mass and inertias from
  `F16_inertia.dml`, with Izx = 982 as a positive product of inertia.
- **The disturbance is ω_ie^b** = (2.5001e-3, −2.3895e-3, −2.3443e-3) deg/s at 36.019° N, ψ = 45°. That
  equals SIM 4's inertial p and r.
- **The moments are taken at the CM** (D4): N_cm = N_mrc − 1.132·Y, in the disturbance and in the side-force
  derivatives alike.
- **Left out:** the longitudinal coupling, the pitch-damping change from the q component, the slow change of
  ω_ie^b along the track, and the kinematic Earth-rate terms of the perturbation.
- **Integration:** RK4 at 1 ms for 180 s.

The result:
- The extra loads are ΔY = −0.087 lbf, ΔL = −1.505 lbf·ft and ΔN = +1.200 lbf·ft at the CM (+1.101 at the
  MRC).
- **The roll relative to the local level reaches −2.37e-4 rad at 8 s, −1.44e-3 rad at 60 s and −2.66e-3 rad
  at 180 s.**
- **Case 11's roll band is 2.686e-5 rad** (#106's report; `kRoll` in the case 11 header). The drift is about 9
  times the band at the first gated sample and **about 99 times it at 180 s**.
- Codex's review (round 1) caught a first version of this estimate that left out the transfer to the CM: it
  gave −2.15e-3 rad at 180 s, about 80 times the band.
- A one-degree-of-freedom roll-subsidence bound gives 9.4e-3 rad at 180 s (τ = 0.33 s, a steady relative roll
  rate of −2.99e-3 deg/s). The full lateral model is the better estimate.

The rate frame alone would fail R5 in case 11's roll channel. Its other channels were not estimated.

### D4. The moment reference

- The aero's moments are about the MRC at 35 % MAC (`F16_aero.dml:167-176`). The README says the "summed aero
  and propulsion forces and moments … must be transferred from the moment reference center (35% of MAC behind
  the wing leading edge) to the center of mass" (`README.html:1830-1831`).
- The CM is at 25 % (§4), so it lies 1.132 ft forward of the MRC. The transfer to the CM is
  M_cm = M_mrc + 1.132·Fz and N_cm = N_mrc − 1.132·Fy, in lbf·ft with forces in lbf. L is unchanged because
  DYCG = DZCG = 0. Thrust lies along body x through the reference point, so it adds no moment.
- Vol II D.1.11 (p.229) says SIM 4 and SIM 5 recorded the aero moments at the CM after the transfer, and SIM 2
  at the MRC. D3's computed check reproduces SIM 5's N only with this transfer: without it the prediction is
  0.3558, not 0.3880.
- The plan's contract already takes the aero channels about the CM. **No disagreement.** The transfer is a
  requirement on PR 6b and a term of Q7's residual.

### D5. The trim residual's terms (Q7, amendment 2)

- **The engine gyroscopic term is zero.** The propulsion has no rotor and no moments (§3), so Q7's fourth part
  vanishes by the files.
- **The aero rate terms** take the Earth-relative rate (D3). Under Q6's convention that is the transport rate
  in body axes, not Q6's full rate. That is SIM 5's case 11 Earth-relative (3.3e-5, −1.550e-3, −7.94e-4) deg/s,
  against its inertial (2.53e-3, −3.94e-3, −3.14e-3).
- **The MRC-to-CM transfer** (D4) is part of the moments.
- So the plan's Q7 text ("the aero model's rate terms at the trim state (the body rates Q6 imposes)") holds
  only with D3's frame.

### D6. The Mach range and the declared Mach limit (ADR-0031)

**The aero has no Mach axis** (§2). P1 states its validity only in words:
- the header's "Subsonic" (`F16_aero.dml:6`);
- the README title, "F-16 Subsonic aircraft …" (`README.html:734`), and its goal, "a maneuvering F-16 subsonic
  vehicle" (`:749`);
- the README's "Flight envelope is limited to the vicinity of 10,000 ft MSL and 287.8 knots equivalent
  airspeed (around Mach 0.5)" (`:910-911`);
- for the laws: "Flight at other speed/altitude combinations may be sub-optimal or even unstable"
  (`:1355`).

**P1 states no numeric Mach limit.** The propulsion's breakpoints end at Mach 1.0 (§3), but ADR-0031's
declaration is the aero model's.

**On the other side:**
- Vol II B.1.5 (p.14) says the model was "chosen for both subsonic and supersonic atmospheric flight
  scenarios".
- C.1.12 (p.63) prints "2000 ft/s (Mach 20)", where SIM 5's CSV gives Mach 2.0104687.

Per the obligation (6a, 13), **case 12's expected nonzero Mach counter (R18, N-07) returns to Joe.** The options
include:
- a Mach limit of 1.0 read from "Subsonic", which keeps R18's case 12 clause;
- no Mach declaration, which drops that clause;
- another reading of the README's "vicinity … around Mach 0.5", which gives no number.

### D7. Versions, and the README's example runs against Vol II's cases

The README's examples are NASA's own illustrations. Vol II's tables define the cases. Both are recorded:

| Item | README | Vol II |
|---|---|---|
| File revisions | aero mod O, control rev E, gnc rev B (`:927-948`) | not stated; the files are Mod P, Rev F, Rev C (§1) |
| Airspeed step | "a speed reduction of 10 knots" (`:1969-1970`) | 13.2: "decrease commanded 5 KEAS" (Table 38, p.64) |
| Lateral offset | "2,000 ft to the left of the original course … `lateralDeviationError` … -2,000 ft or left of course … `trueBaseCourseCommand` … 90 deg" (`:2002-2004`) | 13.4: "commanded to the right" (C.1.16, p.65), at a 45° course (Table 40). The law's sign (§5) turns right for an error of −2,000 ft, which is consistent with Q27 |
| Pole start | 89.95° (printed "89,95"), 0° longitude, 100° true (`:2066`) | case 15: [89.95, −45, 10000], ψ = 90° (Table 42, p.67) |
| IDL start | 0°, −179.95°, 45° true (`:2040`) | case 16: [0, −179.95, 10000], ψ = 0° (Table 43, p.67) |
| Speed while circling | "585.6 ft/s" (`:2045`, `:2070`) | trim 565.6854 ft/s (README Table 11 itself, `:1860`) |

### D8. Small items

- **Figure 7 against the DML.** Vol II Figure 7 (p.23), rendered, prints the 16 LQR gains exactly as
  `F16_control.dml:571-655`. There is no disagreement.
- **Vol II D.1.17's rates.** The prose gives SIM 5's case 15 start as "an Earth-relative roll, pitch, and yaw
  rate of 0.0828 deg/s, −0.0015 deg/s, and 1.7640 deg/s" (p.309). Its CSV's inertial columns read
  0.08279135, −0.00154103 and **−1.76394722**. The prose drops the yaw sign, and its values match the
  inertial columns, not the Earth-relative ones (which differ by ω_ie, about 4e-3 deg/s here).
- **The propulsion's altitude.** It is "Geometric altitude above reference geoid" (`README.html:1161`). The
  plan's assumption, that NASA's `altitudeMsl_ft` is height above the ellipsoid, is left to PR 9.
- **The sign attributes.** The aero gives `beta` as "wind in right ear", control as "+RT" and the README as
  "+ANL". These describe one convention (velocity to the right of the nose) and match ICD v1's β.
  `trueBaseCourseCommand` is "+CCFN" in the file and "+CWFN" in the README and Vol II Table 8.
- **The `atan2` comment and T_MAX's description** (§5, §3) are errors in the file's comments. Neither changes
  the MathML or the data.

## 10. Joe's decisions

All decided by Joe on 2026-10-08.

1. **Q14 and Q15:** confirmed as proposed in §8. This is PR 6a's acceptance item.
2. **Q5, Q12 and Q26.**
   - Q5's condition holds, with §8's sub-condition: the rate the aero model uses comes from the sample's
     state, its environment or the model's own constants, never from a peer binding. PR 5b is not scheduled.
   - Q12: exchange divisor 10.
   - Q26: as proposed in §8. That includes the DML's own constants inside the law and the navigator
     (π = 3.14159265, 6076.12 ft/nm, 60 nm/deg) and the exact knot for KEAS.
3. **D3, the aero's rate frame: (B1).**
   - The core supplies the body rate relative to the Earth, ω_b − ω_ie in body axes. It is computed per RK4
     stage from the configured Earth model, the way the EOM already forms `omega_ie_b`. A flat or
     non-rotating Earth gives ω_ie = 0. The F-16 aero reads it.
   - The template's aero migrates too. Its configs use `earth/flat`, so its values must come out identical,
     and a test shows that.
   - The notional transport and `nesc_aero` keep the inertial ω_b.
   - The notional transport's migration (B2) becomes a PLAN.md open item, measured before Joe decides it.
   - The M7 PR table gains two entries:
     - a new record, ADR-0032 (the next free number; ADR-0029 is reserved for PR 1b), written in PR 1b beside
       ADR-0029;
     - a Tier 3 core PR before PR 6b that implements it, with its `kCoreApiVersion` bump. Claude labeled it
       PR 3b. PR 6b depends on it.
4. **D3, the laws' rate frame: deferred to PR 1b.** An obligation is carried to PR 1b and PR 11a: try to
   decide it from data, as D3's aero check did.
   - Back out el, ail and rdr from the members' aero channels at samples of 13.3 (and 15 if needed).
   - Evaluate the DML law with inertial rates and with Earth-relative rates, and compare.
   - If that cannot discriminate, bring it to Joe, with ICD v1's inertial rates (what a gyro measures) as the
     default.
5. **D1:** the DML's 637.1595 slug, since Vol II calls the `.dml` files normative. R1 is amended: the mass
   equals `F16_inertia.dml`'s, and Table 7's 637.26 is reported beside it.
6. **D6:** the F-16 aero declares Mach ≤ 1.0, read from P1's "Subsonic" under ADR-0031's data-domain rule.
   R18's case 12 clause stands: its Mach counter is nonzero under `warn`, and its other envelope counters stay
   zero.
7. **D2, D4, D5, D7 and D8:** accepted as recorded. D4's MRC-to-CM transfer is a requirement on PR 6b and a
   term of Q7's residual. D5's aero rate terms use D3's Earth-relative rate.
8. **The M7 plan's amendments.** Each is dated 2026-10-08 and points here. Nothing else in the plan changes.
   - Assumption 3: the aero's rates are Earth-relative; the laws' frame stays open until PR 1b.
   - Q7's aero rate-terms text.
   - R1.
   - Q15's final text.
   - The PR table and its dependencies, for item 3: PR 1b gains ADR-0032; PR 3b is added; PR 6b depends on
     PR 3b; PR 3b is Tier 3, and ADR-0032 is accepted before it starts.
   - Q5's sub-condition, and PR 5b not scheduled.
