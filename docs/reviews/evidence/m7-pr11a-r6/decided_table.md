R6's decided runs (the committed configs): every channel whose largest gated ratio exceeds 1
| Case | Channel | First miss | Largest gated ratio (sample) | ASTRA | Band reference | tol_abs | ASTRA - SIM 4 | ASTRA - SIM 5 | SIM 4 - SIM 5 | ASTRA sits |
|---|---|---|---|---|---|---|---|---|---|---|
| 13p1 | density | 80 (8.0 s) | 1.06 (80) | 0.9024449 | 0.9024604 | 1.467e-05 | -1.8e-05 | -1.312e-05 | +4.879e-06 | below both |
| 13p2 | rate_q | 80 (8.0 s) | 3.87 (90) | 0.0006790676 | 0.0006734104 | 1.864e-06 | +5.602e-06 | +5.712e-06 | +1.102e-07 | above both |
| 13p2 | density | 80 (8.0 s) | 1.84 (80) | 0.904419 | 0.9044324 | 7.29e-06 | -1.463e-05 | -1.22e-05 | +2.43e-06 | below both |
| 13p3 | force_y | 156 (15.6 s) | 1.44 (156) | 9692.245 | -21189.58 | 2.145e+04 | +3.271e+04 | +2.905e+04 | -3665 | above both |
| 13p3 | moment_l | 156 (15.6 s) | 2.4 (156) | 211249.9 | -109991.1 | 1.34e+05 | +3.151e+05 | +3.274e+05 | +1.237e+04 | above both |
| 13p3 | moment_n | 156 (15.6 s) | 1.43 (156) | -95250.66 | 65059.72 | 1.117e+05 | -1.68e+05 | -1.526e+05 | +1.543e+04 | below both |
| 13p4 | force_y | 206 (20.6 s) | 1.13 (206) | 9691.142 | -22822.1 | 2.881e+04 | +3.361e+04 | +3.142e+04 | -2187 | above both |
| 13p4 | moment_l | 200 (20.0 s) | 1.53 (206) | 239077.9 | -31357.71 | 2.083e+05 | +3.018e+05 | +2.391e+05 | -6.272e+04 | above both |
| 13p4 | moment_n | 206 (20.6 s) | 1.14 (206) | -95247.89 | 70980.84 | 1.461e+05 | -1.711e+05 | -1.613e+05 | +9787 | below both |

This script's gated maxima against the harness log's: 64 of 64 agree to 1e-3 relative
