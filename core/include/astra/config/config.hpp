// The configuration tree (docs/architecture.md §13, §15): every selectable component is named by
// its plugin path in one YAML document, loaded and validated at init and frozen for the run. This
// header carries no yaml-cpp type: astra_core links yaml-cpp privately behind Config's pimpl, so
// no plugin ever needs the yaml-cpp headers, only this one.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace astra {

// A node in a loaded YAML document plus the dotted path that reached it from the root
// ("vehicle.models.0.plugin"), used to name the source of every error (§15: unknown keys are
// errors, not warnings). Copying a Config is cheap: it shares the document and copies only the
// path string. Every accessor re-resolves from the root, which is what lets a missing key or a
// wrong type be reported with the exact dotted path that caused it.
class Config {
 public:
  // Parses path as YAML; throws ConfigError (errors.hpp) naming path on a missing file or a
  // malformed document.
  [[nodiscard]] static Config from_file(const std::filesystem::path& path);
  // Parses yaml_text as YAML; origin names the source in every error this Config's getters throw
  // (a test fixture that is not a real file, or the file path when the caller already has one).
  [[nodiscard]] static Config from_string(std::string_view yaml_text, std::string_view origin);

  [[nodiscard]] bool has(std::string_view key) const;
  [[nodiscard]] Config at(std::string_view key) const;
  [[nodiscard]] Config at(std::size_t index) const;
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool is_map() const;
  [[nodiscard]] bool is_sequence() const;
  [[nodiscard]] bool is_scalar() const;

  // The dotted path from the document root to this node ("vehicle.models.0"); empty at the root.
  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  // The file or fixture this Config's document was loaded from, for callers that build their own
  // ConfigError (e.g. Simulation reporting an unread key by name after every model has init'd).
  [[nodiscard]] std::string_view origin() const noexcept;

  [[nodiscard]] double number(std::string_view key) const;
  [[nodiscard]] double number(std::string_view key, double default_value) const;
  [[nodiscard]] std::int64_t integer(std::string_view key) const;
  [[nodiscard]] std::int64_t integer(std::string_view key, std::int64_t default_value) const;
  [[nodiscard]] bool boolean(std::string_view key) const;
  [[nodiscard]] bool boolean(std::string_view key, bool default_value) const;
  [[nodiscard]] std::string text(std::string_view key) const;
  [[nodiscard]] std::string text(std::string_view key, std::string_view default_value) const;

  // The scalar value of this node itself, for a sequence entry with no key of its own (e.g. a
  // plain "plugins/physical/earth/flat" plugin-path entry, §13).
  [[nodiscard]] double number() const;
  [[nodiscard]] std::int64_t integer() const;
  [[nodiscard]] bool boolean() const;
  [[nodiscard]] std::string text() const;

  // Every scalar leaf in the whole document that no getter above has read yet, as dotted paths
  // from the root regardless of where this Config sits in the tree (§15: unknown keys are
  // errors). Meaningful only once every model that will read this document has had its init.
  [[nodiscard]] std::vector<std::string> unknown_keys() const;

  // The whole document as YAML text (yaml-cpp's emitter) with the root map's `key` entry left out:
  // ADR-0025 stores a run's config without its "log" block. Scalars keep their source spelling
  // (yaml-cpp holds them as text), so the text loads back to the same values; comments are not
  // kept. Marks nothing as read, wherever this Config sits in the tree. Init-time only.
  [[nodiscard]] std::string emit_without(std::string_view key) const;

 private:
  struct Document;

  Config(std::shared_ptr<Document> document, std::string path);

  std::shared_ptr<Document> document_;
  std::string path_;
};

}  // namespace astra
