# ADR-0009 — Plugin architecture: one core library, one `.so` per selectable component, two roots

Status: **accepted**, 2026-09-14. Elaborates ADR-0000 row "Model plug-in"; `docs/architecture.md` §4, §7, §13.

## Context

The user story is "implement three classes for your aircraft, point the sim at your flight software, run",
with no framework file edited. Reference models, user aircraft, and transports must be swappable by config,
built by one command, and loadable without `dlopen` in a hard-real-time build.

## Decision

One CMake project builds `libastra_core.so` (the kernel: interfaces, scheduler, clock, EOM, integrators,
loader, signal registry, config; knows no aircraft, no hardware) plus one shared object per plugin. A plugin
is one thing a configuration can select (an atmosphere, an engine, an IMU, a transport); its internals are
ordinary classes hidden by `-fvisibility=hidden`. Each plugin exports exactly one C symbol:

```cpp
extern "C" const astra::PluginDescriptor* astra_plugin();
// { const char* category; const char* name; uint32_t core_api_version;
//   const char* built_with; std::unique_ptr<Model> (*create)(); }
```

Two roots, same taxonomy inside (`physical/`, `hardware/`, `sim/`; no `vehicle/` kind): `plugins/` holds
shared models by kind; `aircraft/<name>/` holds one aircraft's own models and configs, copied from
`aircraft/_template/`. The build discovers every `CMakeLists.txt` under both roots; each is one line,
`astra_add_plugin(SOURCES ...)`, which derives the plugin path from its directory, sets output name,
visibility, link to `astra_core`, and registers the plugin for the static build and the load-all test. The
loader resolves a config path (`plugins/physical/atmosphere/isa1976`) to
`libastra_plugins_physical_atmosphere_isa1976.so` in the build's plugin directory, `dlopen`s it at init
only, checks `core_api_version` and `built_with` against its own, and raises a typed init error naming the
plugin on mismatch. `ASTRA_STATIC_PLUGINS` links every plugin into one binary and registers the same
descriptors by direct call. Nobody ships or imports prebuilt `.so` files. Integrators are core, not plugins.

## Rejected alternatives

- **Static factory compiled into the core.** Users must edit the framework build to add a model; the
  on-ramp (§18) fails at step 3.
- **Header-only / template models.** No runtime swap by config, and a worse Python story (trampolines
  need virtual bases).
- **Importing prebuilt `.so` files.** ABI drift between core and plugin builds; explicitly not wanted.
  Every plugin is built in the same invocation as the core, so a version mismatch can only be a stale build
  directory, which the descriptor check makes loud.
- **A single by-kind tree with aircraft scattered across it.** Nothing a user can point at and call
  "mine"; copying an aircraft becomes a scavenger hunt.
- **A `vehicle/` kind alongside `physical/` and `hardware/`.** Wrong axis: the aircraft folder is the
  ownership boundary, not a taxonomy; everything on a vehicle is physics or equipment.

## Consequences

- M0 acceptance: the loader loads a dummy plugin by path, rejects a bad `core_api_version` with a typed
  error, and the static-plugins build runs the same test without `dlopen`.
- Granularity rule: internals of a high-fidelity model stay inside its plugin as ordinary classes.
- Anything that knows a wing area, thrust curve, or noise density lives under `plugins/` or `aircraft/`.
- The loaded plugin set with versions and build strings is recorded in every log (ADR-0005).
