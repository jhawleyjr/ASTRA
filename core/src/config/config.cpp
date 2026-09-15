#include "astra/config/config.hpp"

#include <yaml-cpp/yaml.h>

#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "astra/errors.hpp"

namespace astra {

struct Config::Document {
  YAML::Node root;
  std::string origin;
  std::set<std::string> read_paths;
};

namespace {

std::string join(std::string_view base, std::string_view segment) {
  if (base.empty()) {
    return std::string{segment};
  }
  std::string joined{base};
  joined += '.';
  joined += segment;
  return joined;
}

std::string join_index(std::string_view base, std::size_t index) {
  return join(base, std::to_string(index));
}

constexpr std::size_t kDecimalBase = 10;

bool parse_index(std::string_view segment, std::size_t& out) {
  if (segment.empty()) {
    return false;
  }
  std::size_t value = 0;
  for (const char c : segment) {
    if (c < '0' || c > '9') {
      return false;
    }
    value = (value * kDecimalBase) + static_cast<std::size_t>(c - '0');
  }
  out = value;
  return true;
}

// yaml-cpp's operator[] on a non-const Node inserts a missing key as a null "zombie" node
// (docs/lessons.md), silently growing the document. Every lookup below goes through a
// const-qualified node so the const overload -- which only ever reads -- is the one selected.

// A genuinely undefined node (IsDefined() == false), matching what a const map lookup returns for a
// missing key. A bare `YAML::Node{}` does NOT match that: yaml-cpp's default constructor produces a
// Null-typed node that is still "defined" (docs/lessons.md), so returning `{}` from child() below
// as a "not found" sentinel would make navigate_from's `!next.IsDefined()` short-circuit fail to
// fire -- navigation past a scalar (or a too-short sequence) would silently continue instead of
// stopping, and the final result would be this same Null-but-"defined" node instead of a correctly
// missing one.
YAML::Node undefined_node() {
  const YAML::Node empty_map{YAML::NodeType::Map};
  return empty_map["astra_internal_never_a_real_key"];
}

YAML::Node child(const YAML::Node& node, std::string_view segment) {
  std::size_t index = 0;
  if (parse_index(segment, index)) {
    if (!node.IsSequence() || index >= node.size()) {
      return undefined_node();
    }
    return node[index];
  }
  if (!node.IsMap()) {
    return undefined_node();
  }
  return node[std::string{segment}];
}

std::vector<std::string_view> split(std::string_view path) {
  std::vector<std::string_view> segments;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t dot = path.find('.', start);
    if (dot == std::string_view::npos) {
      segments.push_back(path.substr(start));
      break;
    }
    segments.push_back(path.substr(start, dot - start));
    start = dot + 1;
  }
  return segments;
}

// Recursive, not a loop with a reassigned "current" node: yaml-cpp's Node::operator= does not
// rebind a handle to point elsewhere in the tree -- it mutates the node the handle already
// referenced in place (so that "node[key] = value" can build documents), so `current =
// child(current, segment)` in a loop corrupts root itself, one level at a time, out from under the
// caller (docs/lessons.md). Every step here introduces a brand-new Node via copy-construction
// instead, which never triggers that path.
YAML::Node navigate_from(const YAML::Node& node, std::span<const std::string_view> segments) {
  if (segments.empty()) {
    return node;
  }
  const YAML::Node next = child(node, segments.front());
  if (!next.IsDefined()) {
    return next;
  }
  return navigate_from(next, segments.subspan(1));
}

YAML::Node navigate(const YAML::Node& root, std::string_view path) {
  if (path.empty()) {
    return root;
  }
  const std::vector<std::string_view> segments = split(path);
  return navigate_from(root, segments);
}

// A leaf is a scalar, a null, or an EMPTY map/sequence (Codex review, PR M1-2): without the empty
// case, "bogus: {}" or "bogus: []" had no leaf under them at all and so could never appear in
// unknown_keys(), silently defeating §15's "unknown keys are errors". Config::at() marks the exact
// path it returns as read, so a caller that explicitly navigated to and inspected an empty
// subtree (checked its size, say) is not falsely flagged by this.
void collect_leaf_paths(const YAML::Node& node, const std::string& prefix,
                        std::vector<std::string>& out) {
  if (node.IsMap() && node.size() > 0) {
    for (const auto& entry : node) {
      collect_leaf_paths(entry.second, join(prefix, entry.first.as<std::string>()), out);
    }
  } else if (node.IsSequence() && node.size() > 0) {
    for (std::size_t i = 0; i < node.size(); ++i) {
      collect_leaf_paths(node[i], join_index(prefix, i), out);
    }
  } else {
    out.push_back(prefix);
  }
}

[[noreturn]] void throw_missing(std::string_view origin, const std::string& path) {
  throw ConfigError(origin, path, "missing config key");
}

[[noreturn]] void throw_wrong_type(std::string_view origin, const std::string& path,
                                   std::string_view expected) {
  throw ConfigError(origin, path, "expected a " + std::string{expected} + " value");
}

template <typename T>
T scalar_as(const YAML::Node& node, std::string_view origin, const std::string& path,
            std::string_view type_name) {
  try {
    return node.as<T>();
  } catch (const YAML::Exception&) {
    throw_wrong_type(origin, path, type_name);
  }
}

// Takes the document apart into its three pieces instead of a Config::Document& so these free
// functions -- unlike Config's own member functions -- never need to name the private nested type.
// Reading a value also records the path as consumed (Config::unknown_keys), which is the whole
// reason a getter that is logically read-only still mutates read_paths.
template <typename T>
T read_scalar(const YAML::Node& root, std::string_view origin, std::set<std::string>& read_paths,
              const std::string& full_path, std::string_view type_name) {
  const YAML::Node node = navigate(root, full_path);
  if (!node.IsDefined()) {
    throw_missing(origin, full_path);
  }
  T value = scalar_as<T>(node, origin, full_path, type_name);
  read_paths.insert(full_path);
  return value;
}

template <typename T>
T read_scalar_or(const YAML::Node& root, std::string_view origin, std::set<std::string>& read_paths,
                 const std::string& full_path, std::string_view type_name, T default_value) {
  const YAML::Node node = navigate(root, full_path);
  if (!node.IsDefined()) {
    return default_value;
  }
  T value = scalar_as<T>(node, origin, full_path, type_name);
  read_paths.insert(full_path);
  return value;
}

}  // namespace

Config::Config(std::shared_ptr<Document> document, std::string path)
    : document_(std::move(document)), path_(std::move(path)) {}

Config Config::from_file(const std::filesystem::path& path) {
  YAML::Node root;
  try {
    root = YAML::LoadFile(path.string());
  } catch (const YAML::Exception& error) {
    throw ConfigError(path.string(), "", std::string{"malformed YAML: "} + error.what());
  }
  auto document =
      std::make_shared<Document>(std::move(root), path.string(), std::set<std::string>{});
  return {std::move(document), ""};
}

Config Config::from_string(std::string_view yaml_text, std::string_view origin) {
  YAML::Node root;
  try {
    root = YAML::Load(std::string{yaml_text});
  } catch (const YAML::Exception& error) {
    throw ConfigError(origin, "", std::string{"malformed YAML: "} + error.what());
  }
  auto document =
      std::make_shared<Document>(std::move(root), std::string{origin}, std::set<std::string>{});
  return {std::move(document), ""};
}

std::string_view Config::origin() const noexcept { return document_->origin; }

bool Config::has(std::string_view key) const {
  return navigate(document_->root, join(path_, key)).IsDefined();
}

Config Config::at(std::string_view key) const {
  const std::string full_path = join(path_, key);
  if (!navigate(document_->root, full_path).IsDefined()) {
    throw_missing(document_->origin, full_path);
  }
  // Marks full_path read even though at() returns a subtree rather than a value: an empty map or
  // sequence is itself a leaf (collect_leaf_paths above), so a caller that explicitly navigated
  // to and inspected one (its size, say) has acknowledged it and unknown_keys() must not still
  // flag it.
  document_->read_paths.insert(full_path);
  return {document_, full_path};
}

Config Config::at(std::size_t index) const {
  const std::string full_path = join_index(path_, index);
  if (!navigate(document_->root, full_path).IsDefined()) {
    throw_missing(document_->origin, full_path);
  }
  document_->read_paths.insert(full_path);
  return {document_, full_path};
}

std::size_t Config::size() const { return navigate(document_->root, path_).size(); }
bool Config::is_map() const { return navigate(document_->root, path_).IsMap(); }
bool Config::is_sequence() const { return navigate(document_->root, path_).IsSequence(); }
bool Config::is_scalar() const { return navigate(document_->root, path_).IsScalar(); }

double Config::number(std::string_view key) const {
  return read_scalar<double>(document_->root, document_->origin, document_->read_paths,
                             join(path_, key), "number");
}
double Config::number(std::string_view key, double default_value) const {
  return read_scalar_or<double>(document_->root, document_->origin, document_->read_paths,
                                join(path_, key), "number", default_value);
}
std::int64_t Config::integer(std::string_view key) const {
  return read_scalar<std::int64_t>(document_->root, document_->origin, document_->read_paths,
                                   join(path_, key), "integer");
}
std::int64_t Config::integer(std::string_view key, std::int64_t default_value) const {
  return read_scalar_or<std::int64_t>(document_->root, document_->origin, document_->read_paths,
                                      join(path_, key), "integer", default_value);
}
bool Config::boolean(std::string_view key) const {
  return read_scalar<bool>(document_->root, document_->origin, document_->read_paths,
                           join(path_, key), "boolean");
}
bool Config::boolean(std::string_view key, bool default_value) const {
  return read_scalar_or<bool>(document_->root, document_->origin, document_->read_paths,
                              join(path_, key), "boolean", default_value);
}
std::string Config::text(std::string_view key) const {
  return read_scalar<std::string>(document_->root, document_->origin, document_->read_paths,
                                  join(path_, key), "string");
}
std::string Config::text(std::string_view key, std::string_view default_value) const {
  return read_scalar_or<std::string>(document_->root, document_->origin, document_->read_paths,
                                     join(path_, key), "string", std::string{default_value});
}

double Config::number() const {
  return read_scalar<double>(document_->root, document_->origin, document_->read_paths, path_,
                             "number");
}
std::int64_t Config::integer() const {
  return read_scalar<std::int64_t>(document_->root, document_->origin, document_->read_paths, path_,
                                   "integer");
}
bool Config::boolean() const {
  return read_scalar<bool>(document_->root, document_->origin, document_->read_paths, path_,
                           "boolean");
}
std::string Config::text() const {
  return read_scalar<std::string>(document_->root, document_->origin, document_->read_paths, path_,
                                  "string");
}

std::vector<std::string> Config::unknown_keys() const {
  std::vector<std::string> leaves;
  collect_leaf_paths(document_->root, "", leaves);
  std::vector<std::string> unknown;
  for (const std::string& leaf : leaves) {
    if (!document_->read_paths.contains(leaf)) {
      unknown.push_back(leaf);
    }
  }
  return unknown;
}

}  // namespace astra
