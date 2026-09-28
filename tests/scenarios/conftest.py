"""The scenario harness's pytest hook (M5 PR 6; docs/plans/m5-tooling.md Q14, R11 to R13).

Each ``*.yaml`` under ``tests/scenarios/`` is one item, run by ``astra.scenario``. An invalid
scenario is a collection error, never a skip. A ``*.yml`` file is refused rather than silently
ignored. The item writes its log into a temporary directory that is removed afterwards.
"""

from __future__ import annotations

import tempfile
from pathlib import Path
from typing import Any

import pytest

from astra import scenario as harness


def pytest_collect_file(parent: pytest.Collector, file_path: Path) -> pytest.Collector | None:
    if file_path.suffix in (".yaml", ".yml"):
        return ScenarioFile.from_parent(parent, path=file_path)
    return None


class ScenarioFile(pytest.File):
    def collect(self) -> list[pytest.Item]:
        if self.path.suffix != ".yaml":
            raise self.CollectError(f"{self.path}: scenario files use the .yaml suffix")
        try:
            loaded = harness.load(self.path, root=self.config.rootpath)
        except harness.ScenarioError as error:
            raise self.CollectError(str(error)) from error
        return [ScenarioItem.from_parent(self, name=self.path.stem, scenario=loaded)]


class ScenarioItem(pytest.Item):
    def __init__(self, *, scenario: harness.Scenario, **kwargs: Any) -> None:
        super().__init__(**kwargs)
        self.scenario = scenario

    def runtest(self) -> None:
        with tempfile.TemporaryDirectory(prefix="astra_scenario_") as directory:
            result = harness.run(self.scenario, Path(directory) / f"{self.name}.h5")
        if result.failures:
            raise harness.ScenarioFailedError(self.scenario, result.failures)

    def repr_failure(
        self,
        excinfo: pytest.ExceptionInfo[BaseException],
        style: Any = None,
    ) -> str | Any:
        if isinstance(excinfo.value, harness.ScenarioFailedError):
            return str(excinfo.value)
        return super().repr_failure(excinfo, style)

    def reportinfo(self) -> tuple[Path, int | None, str]:
        return self.path, None, f"scenario {self.name}"
