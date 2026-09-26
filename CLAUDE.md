@AGENTS.md

## Claude Code

Read [Claude Code procedures](docs/agents/claude-code.md) before implementation or review.
Use `claude --worktree <name>` for new tasks; preserve an explicitly assigned checkout.
Keep the model/effort settings in `.claude/settings.json`; only Joe changes them.
Use `/wrap` for a stage-aware handoff, including blocked work; it pushes the task branch and opens or updates the draft PR.
Use `/plan-feature`, `/review-cycle`, and `/human-review` for the corresponding workflow stages.
