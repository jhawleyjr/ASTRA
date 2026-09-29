# Repository documentation

Start with [AGENTS.md](../AGENTS.md) and the [development workflow](process/workflow.md).

| Location | Purpose |
|---|---|
| [architecture.md](architecture.md) | Accepted design and milestone acceptance; keep the existing single document |
| [decisions/](decisions/) | Human-owned ADRs; accepted records are immutable |
| [../PLAN.md](../PLAN.md), [../STATE.md](../STATE.md) | Milestones and current session handoff |
| [future-work.md](future-work.md) | Unscheduled future-work candidates; PLAN.md takes an item when it is planned |
| `specs/`, `plans/`, `tasks/` | Feature intent, repository-aware plan, bounded work packages (see [templates](templates/README.md)) |
| [reviews/](reviews/) | Findings and human packets when PR text is insufficient |
| [templates/](templates/) | Copy only the artifacts the change needs |
| [agents/](agents/) | Shared role procedures and Claude-specific instructions |
| [process/validation.md](process/validation.md) | Existing executable checks and evidence |
| [lessons.md](lessons.md), [refs/](refs/) | Append-only lessons and human-supplied reference material |

Link to authoritative material instead of copying it. The architecture describes some future paths
(such as `embedded/` and `aircraft/`) that are not implemented yet; inspect the tree before planning.
