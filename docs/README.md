# Repository documentation

Start with [AGENTS.md](../AGENTS.md) and the [development workflow](process/workflow.md).

| Location | Purpose |
|---|---|
| [../README.md](../README.md) | What ASTRA is, its features, quick start and how-tos |
| [architecture.md](architecture.md) | Accepted design and milestone acceptance; keep the existing single document |
| [decisions/](decisions/) | Human-owned ADRs; accepted records are immutable |
| [reference/config-keys.md](reference/config-keys.md) | Every plugin's config keys, generated from each plugin's key block by `scripts/config_keys.py` |
| [../PLAN.md](../PLAN.md), [../STATE.md](../STATE.md) | Milestones and current session handoff |
| [future-work.md](future-work.md) | Unscheduled future-work candidates; PLAN.md takes an item when it is planned |
| `specs/`, `plans/`, `tasks/` | Feature intent, repository-aware plan, bounded work packages (see [templates](templates/README.md)) |
| [reviews/](reviews/) | Findings and human packets when PR text is insufficient |
| [templates/](templates/) | Copy only the artifacts the change needs |
| [agents/](agents/) | Shared role procedures and Claude-specific instructions |
| [process/validation.md](process/validation.md) | Existing executable checks and evidence |
| [lessons.md](lessons.md), [refs/](refs/) | Append-only lessons and human-supplied reference material |

Link to authoritative material instead of copying it. The architecture also describes paths that are not
built yet (such as `plugins/hardware/`, `plugins/sim/transport/fpga/` and `tools/`) and marks some features
*planned*; inspect the tree before planning.
