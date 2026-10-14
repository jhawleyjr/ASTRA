The 13.x runs after Joe's 2026-10-13 decision (Q11 amended; R6 gated per item 3)

Decided runs (divisor 10, the trim's references), every gated sample from 80 on:

| Case | Asserted channels | Largest asserted ratio (sample) | Asserted channels above 1 | Reported channels above 1 (sample; first gated exceedance) |
|---|---|---|---|---|
| 13.1 | 21 | density 0.418 (80) | none | (none reported) |
| 13.2 | 21 | rate_q 0.786 (98) | none | (none reported) |
| 13.3 | 15 | roll 0.798 (156) | none | force_y 1.44 (156; first 156), moment_l 2.4 (156; first 156), moment_n 1.44 (156; first 156) |
| 13.4 | 15 | roll 0.938 (206) | none | force_y 1.13 (206; first 206), moment_l 1.52 (206; first 200), moment_n 1.14 (206; first 206) |

Each 13.3 and 13.4 aero channel's largest gated ratio (reported, never asserted; R6 not met):

| Case | force_x | force_y | force_z | moment_l | moment_m | moment_n |
|---|---|---|---|---|---|---|
| 13.3 | 0.176 (158) | 1.44 (156) | 0.254 (158) | 2.4 (156) | 0.148 (158) z | 1.44 (156) |
| 13.4 | 0.157 (209) | 1.13 (206) | 0.238 (208) | 1.52 (206) | 0.142 (208) z | 1.14 (206) |

(z: zero-accepting, never cited as evidence.)

Reported variants, no band asserted, every channel at every gated sample:

| Run | Largest discriminating ratio (sample) | Inside every band the decided run asserts | Channels above 1 (sample; first gated exceedance) |
|---|---|---|---|
| case13p1 DML references | density 1.06 (80) | NO | density 1.06 (80; first 80) |
| case13p2 DML references | rate_q 3.87 (90) | NO | rate_q 3.87 (90; first 80), density 1.84 (80; first 80) |
| case13p3 DML references | moment_l 2.4 (156) | yes | force_y 1.44 (156; first 156), moment_l 2.4 (156; first 156), moment_n 1.44 (156; first 156) |
| case13p4 DML references | moment_l 1.52 (206) | yes | force_y 1.13 (206; first 206), moment_l 1.52 (206; first 200), moment_n 1.14 (206; first 206) |
| case13p1 divisor 1 (trim references) | v_down 0.411 (83) | yes | none |
| case13p2 divisor 1 (trim references) | density 0.562 (80) | yes | none |
| case13p3 divisor 1 (trim references) | v_down 0.198 (155) | yes | none |
| case13p4 divisor 1 (trim references) | moment_l 1.3 (200) | yes | moment_l 1.3 (200; first 200) |
| case13p4 rhumb line | moment_l 1.52 (206) | - | force_y 1.13 (206; first 206), moment_l 1.52 (206; first 200), moment_n 1.14 (206; first 206) |
| case13p4 great circle | moment_l 1.52 (206) | - | force_y 1.13 (206; first 206), moment_l 1.52 (206; first 200), moment_n 1.14 (206; first 206) |
| case13p4 line at the offset frame | moment_l 1.52 (206) | - | force_y 1.13 (206; first 206), moment_l 1.52 (206; first 200), moment_n 1.14 (206; first 206) |

The swap against commit 6a's logs (gated max, its sample, first gated exceedance):
- 13.1: 6a decided against DML references: 21 of 21 channels equal
- 13.1: 6a SIM 5 against amended decided: 21 of 21 channels equal
- 13.2: 6a decided against DML references: 21 of 21 channels equal
- 13.2: 6a SIM 5 against amended decided: 21 of 21 channels equal
- 13.3: 6a decided against DML references: 21 of 21 channels equal
- 13.3: 6a SIM 5 against amended decided: 21 of 21 channels equal
- 13.4: 6a decided against DML references: 21 of 21 channels equal
- 13.4: 6a SIM 5 against amended decided: 21 of 21 channels equal

Commit 6b's controls (item 6), from amended_controls.log:

| Control | Asserted | The named channel's ratio (first gated exceedance) | Inside every band the decided run asserts | Channels above 1 (sample; first gated exceedance) |
|---|---|---|---|---|
| case13p1 Izx sign | reported | - | yes | none |
| case13p2 Izx sign | reported | - | yes | none |
| case13p3 Izx sign | reported | - | NO | latitude 1.07 (300; first 257), moment_l 2.17 (156; first 156) |
| case13p4 Izx sign | reported | - | yes | moment_l 1.38 (206; first 200) |
| case13p1 elevator sign | pitch | pitch 76.3 (80) | - | latitude 232 (80; first 80), longitude 218 (80; first 80), altitude 2.35e+03 (80; first 80), v_north 976 (80; first 80), v_east 1.03e+03 (80; first 80), v_down 5.29e+03 (80; first 80), yaw 4.23e+05 (80; first 80), pitch 76.3 (80; first 80), roll 1.81e+05 (80; first 80), rate_p 5.5e+05 (80; first 80), rate_q 4.78e+03 (80; first 80), rate_r 1.1e+05 (80; first 80), force_x 23.3 (80; first 80), force_y 4.84e+04 (80; first 80) zero-accepting, force_z 95.2 (80; first 80), moment_l 2.41e+03 (80; first 80) zero-accepting, moment_m 3.15 (80; first 80) zero-accepting, moment_n 6.96e+03 (80; first 80) zero-accepting, density 3.53e+03 (80; first 80), pressure 1.25e+03 (80; first 80), temperature 2.35e+03 (80; first 80) |
| case13p2 elevator sign | pitch | pitch 436 (80) | - | latitude 236 (80; first 80), longitude 222 (80; first 80), altitude 3.44e+03 (80; first 80), v_north 958 (80; first 80), v_east 1.01e+03 (80; first 80), v_down 2.82e+04 (80; first 80), yaw 4.11e+06 (80; first 80), pitch 436 (80; first 80), roll 4.51e+05 (80; first 80), rate_p 1.71e+07 (80; first 80), rate_q 6.55e+05 (80; first 80), rate_r 1.66e+07 (80; first 80), force_x 356 (80; first 80), force_y 3.36e+06 (80; first 80), force_z 2.82e+03 (80; first 80), moment_l 2.9e+05 (80; first 80) zero-accepting, moment_m 89.1 (80; first 80) zero-accepting, moment_n 5.14e+05 (80; first 80) zero-accepting, density 6.82e+03 (80; first 80), pressure 1.46e+03 (80; first 80), temperature 3.44e+03 (80; first 80) |
| case13p3 elevator sign | pitch | pitch 5.58 (80) | - | latitude 288 (80; first 80), longitude 66.5 (80; first 80), altitude 900 (80; first 80), v_north 120 (80; first 80), v_east 89.2 (80; first 80), v_down 195 (80; first 80), yaw 20.2 (80; first 80), pitch 5.58 (80; first 80), roll 19.9 (80; first 80), rate_p 4.89 (80; first 80), rate_q 10 (80; first 80), rate_r 5.99 (80; first 80), force_x 1.29 (80; first 80), force_y 3.96 (80; first 80), force_z 3.31 (80; first 80), density 1.05e+03 (80; first 80), pressure 673 (80; first 80), temperature 900 (80; first 80) |
| case13p4 elevator sign | pitch | pitch 5.17 (80) | - | latitude 32 (80; first 80), longitude 84.5 (80; first 80), altitude 879 (80; first 80), v_north 127 (80; first 80), v_east 93 (80; first 80), v_down 178 (80; first 80), yaw 18.6 (80; first 80), pitch 5.17 (80; first 80), roll 24.4 (80; first 80), rate_p 6.37 (80; first 80), rate_q 8.78 (80; first 80), rate_r 6.07 (80; first 80), force_x 1.23 (80; first 80), force_y 2.94 (80; first 80), force_z 3.32 (80; first 80), density 1.02e+03 (80; first 80), pressure 662 (80; first 80), temperature 879 (80; first 80) |
| case13p3 aileron sign | roll | roll 2.7 (80) | - | latitude 597 (80; first 80), longitude 75.2 (80; first 80), altitude 750 (80; first 80), v_north 179 (80; first 80), v_east 114 (80; first 80), v_down 115 (80; first 80), yaw 59.3 (80; first 80), pitch 76.8 (80; first 80), roll 2.7 (80; first 80), rate_q 1.66 (80; first 80), rate_r 1.52 (80; first 80), force_x 2.1 (80; first 80), density 874 (80; first 80), pressure 559 (80; first 80), temperature 750 (80; first 80) |
| case13p4 aileron sign | roll | roll 3.31 (80) | - | latitude 66.4 (80; first 80), longitude 95.5 (80; first 80), altitude 732 (80; first 80), v_north 190 (80; first 80), v_east 118 (80; first 80), v_down 105 (80; first 80), yaw 54.6 (80; first 80), pitch 71.2 (80; first 80), roll 3.31 (80; first 80), rate_q 1.46 (80; first 80), rate_r 1.54 (80; first 80), force_x 2 (80; first 80), density 851 (80; first 80), pressure 549 (80; first 80), temperature 732 (80; first 80) |

amended table: OK
