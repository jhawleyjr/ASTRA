# M4 PR 5 mutation checks

Run in this session on the autopilot sources at `297edca` content (the product files are unchanged at `56ea2ec`;
only test files changed since, for clang-tidy). Each mutation was applied to one file, the affected targets rebuilt
(asan preset), `ctest --preset asan -R '(Autopilot|EmbeddedBoundary)'` run, and the file restored. Driver:
session scratchpad `mutate.py` (not in the repo). "Caught" means at least one test failed.

| Mutation | Result | Failing tests (first ones) |
|---|---|---|
| dt ignores timestamps (fixed 0.01) | caught | Autopilot.IntegralScalesWithTheFrameSpacingNotAnAssumedPeriod, ...FrameZeroRepeatedTimeAndBackwardsTimeDoNotIntegrate, ...NonFiniteTimeDoesNotIntegrate... |
| elevator sign flipped | caught | Autopilot.ElevatorAltitudeLoopSignAndValue, ...ElevatorClimbRateDampsNoseUpMotion, and 5 more |
| elevator (output) clamp removed | caught | Autopilot.BankCommandIsClampedBeforeTheBankLoop, ...SaturatedLoopDoesNotWindUp, ...EveryOutputStaysInsideItsLimitUnderViolentInput, ...SaturationAtTheLowLimitAlsoHoldsTheIntegral |
| anti-windup removed | caught | Autopilot.SaturatedLoopDoesNotWindUp, Autopilot.SaturationAtTheLowLimitAlsoHoldsTheIntegral |
| WIND_INVALID not held | caught | Autopilot.WindInvalidHoldsEvenWithFiniteFields, ...ReplayingTheSameScriptIsBitIdentical, AutopilotProcess.RepliesMatchTheLibrary... |
| ATMOSPHERE_INVALID also held | caught | Autopilot.StatusBitsThatTouchNoReadFieldAreIgnored |
| previous time frozen on held frames | caught | Autopilot.HeldFramesLeaveLoopStateAloneAndRecoveryIntegratesOnePeriod, and 2 more |
| heading not wrapped | caught | Autopilot.HeadingErrorWrapsTheShortWay |
| bank command not clamped | caught | Autopilot.BankCommandIsClampedBeforeTheBankLoop |
| theta feedback on absolute theta | caught | Autopilot.FrameZeroReplyAtSetpointEqualsTrimExactly, and many more |
| mode_flags zero | caught | Autopilot.FrameZeroReplyAtSetpointEqualsTrimExactly, ...EveryReplyCarriesTheConstantModeWord..., AutopilotProcess.FirstReplyAtSetpointIsTheTrim |
| exit status remapped to 0 | caught | AutopilotProcess.SilentSimExitsWithTheHalTimeoutStatus, ...MissingSegment..., ...ForeignLayout..., ...RequestDisagreeing... |
| a clock read in the loop (`std::chrono::steady_clock::now()`) | caught | EmbeddedBoundary_autopilot_reads_no_clock |
| an `astra_core` header in the include closure | caught | EmbeddedBoundary_autopilot_include_closure |
| the autopilot links `astra::core` | caught | EmbeddedBoundary_autopilot_links_no_astra_core |

Three first attempts did not build (unused variable / unused function under `-Werror`); they were re-made as
compiling mutations and then caught, as listed. The design test's margin machinery is separately shown live by
`test_margin_analysis_can_fail` (an elevator loop at a tenth of its gains: stable, phase margin 38 degrees).
