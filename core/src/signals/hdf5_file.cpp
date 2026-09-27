#include "astra/signals/hdf5_file.hpp"

#include <hdf5.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "astra/errors.hpp"

namespace astra::hdf5 {

static_assert(std::is_same_v<hid_t, std::int64_t>, "Handle stores hid_t as std::int64_t");
static_assert(std::is_same_v<herr_t, int>, "Handle::CloseFunction is herr_t (*)(hid_t)");

namespace {

// Set by every HDF5 failure the wrapper sees (fail_hdf5, a failed close). Single-threaded use;
// atomic only so the exit handler's read is well defined whatever thread exits.
std::atomic<bool> g_hdf5_failed{false};

void note_hdf5_failure() noexcept { g_hdf5_failed.store(true); }

// M5 PR 4: libhdf5 1.10.10 segfaults in its own exit-time teardown (H5_term_library ->
// H5F__close_cb) once a file close has failed, or when the teardown's own close of a still-open
// file cannot flush (a full disk, EFBIG): a run whose log hit an I/O error would otherwise crash
// at exit after reporting the error cleanly. So the library's atexit hook is turned off and this
// one closes the library only when no HDF5 call failed in this process. After a failure, the
// library is left to the process's end: an open file's handle is released by the kernel, unflushed
// (such a file never has complete = 1), and LeakSanitizer reports nothing because HDF5's global
// allocations are still reachable. tests/unit/kernel/run_log_test.cpp's
// RunLogFailure.ARealWriteErrorHaltsKLog... exits through this path.
void close_library_at_exit() {
  if (!g_hdf5_failed.load()) {
    static_cast<void>(H5close());
  }
}

// Runs when libastra_core is loaded, before any HDF5 call of this library. H5dont_atexit() must
// precede every other HDF5 call in the process; with libhdf5 1.10.10 it returns a failure once the
// library has been initialized (or once anyone has called it), and then HDF5's own exit hook, which
// crashes after a failed close, stays installed and is not this library's to remove (Codex review
// round 1, F2). kOwnsTeardown records which case this process is in; File::create refuses to write
// a file when it is false, since a write failure could then crash the process at exit.
//
// LeakSanitizer and the uninstrumented libhdf5 (plan m5-tooling.md, PR 2) need no suppression: the
// handler above is registered with atexit() at load, after ASan registered its exit-time leak check
// at startup, so atexit's reverse order runs H5close() before the leak scan. Hdf5LsanProbe
// (tests/unit/signals/CMakeLists.txt) shows both halves: a clean process passes LSan, a
// deliberately leaking one is reported.
bool take_library_teardown() noexcept {
  if (H5dont_atexit() < 0) {
    return false;
  }
  static_cast<void>(std::atexit(close_library_at_exit));
  return true;
}

const bool kOwnsTeardown = take_library_teardown();

// HDF5 prints its error stack to stderr on every failure by default; the wrapper reports failures
// as Hdf5Error instead. Called before any file is created or opened.
void silence_error_printing() {
  static const bool kSilenced = H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr) >= 0;
  static_cast<void>(kSilenced);
}

struct StackText {
  std::string outermost;
  std::string innermost;
};

herr_t collect_stack_entry(unsigned index, const H5E_error2_t* entry, void* data) {
  auto* text = static_cast<StackText*>(data);
  std::string line = entry->func_name != nullptr ? entry->func_name : "?";
  line += "(): ";
  line += entry->desc != nullptr ? entry->desc : "";
  if (index == 0) {
    text->outermost = line;
  }
  text->innermost = std::move(line);
  return 0;
}

// The current HDF5 error stack as "outermost <- innermost", then cleared. Read it before any
// further HDF5 call: each API call clears the stack on entry.
std::string take_error_stack() {
  StackText text;
  static_cast<void>(H5Ewalk2(H5E_DEFAULT, H5E_WALK_DOWNWARD, collect_stack_entry, &text));
  static_cast<void>(H5Eclear2(H5E_DEFAULT));
  if (text.outermost.empty()) {
    return "no HDF5 error stack";
  }
  if (text.outermost == text.innermost) {
    return text.outermost;
  }
  return text.outermost + " <- " + text.innermost;
}

[[noreturn]] void fail(const std::string& file, const std::string& object,
                       std::string_view message) {
  throw Hdf5Error(file, object, message);
}

[[noreturn]] void fail_hdf5(const std::string& file, const std::string& object,
                            std::string_view message) {
  note_hdf5_failure();
  std::string text{message};
  text += " (";
  text += take_error_stack();
  text += ")";
  throw Hdf5Error(file, object, text);
}

// The open object an operation acts on, for error messages.
struct Where {
  hid_t id;
  const std::string& file;
  const std::string& object;

  [[noreturn]] void fail(std::string_view message) const { hdf5::fail(file, object, message); }
  [[noreturn]] void fail_hdf5(std::string_view message) const {
    hdf5::fail_hdf5(file, object, message);
  }
};

// A variable-length string attribute's buffer is an array of char pointers that HDF5 takes as
// void*: the one place the wrapper converts a pointer-to-pointer to void.
void* string_buffer(char** strings) {
  return strings;  // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
}
const void* string_buffer(const char* const* strings) {
  return strings;  // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
}

hid_t stored_type(ElementType type) {
  return type == ElementType::kFloat64 ? H5T_IEEE_F64LE : H5T_STD_U64LE;
}

hid_t memory_type(ElementType type) {
  return type == ElementType::kFloat64 ? H5T_NATIVE_DOUBLE : H5T_NATIVE_UINT64;
}

std::string_view type_name(ElementType type) {
  return type == ElementType::kFloat64 ? "float64" : "uint64";
}

Handle variable_string_type(const Where& at) {
  Handle type{H5Tcopy(H5T_C_S1), &H5Tclose};
  if (!type.valid() || H5Tset_size(type.id(), H5T_VARIABLE) < 0 ||
      H5Tset_cset(type.id(), H5T_CSET_UTF8) < 0) {
    at.fail_hdf5("cannot build the variable-length string type");
  }
  return type;
}

Handle scalar_space(const Where& at) {
  Handle space{H5Screate(H5S_SCALAR), &H5Sclose};
  if (!space.valid()) {
    at.fail_hdf5("cannot create a scalar dataspace");
  }
  return space;
}

// Creates attribute `name` on at.id, replacing any existing one, and writes data into it.
void write_attribute(const Where& at, std::string_view name, hid_t file_type, hid_t mem_type,
                     hid_t space, const void* data) {
  const std::string key{name};
  const htri_t exists = H5Aexists(at.id, key.c_str());
  if (exists < 0) {
    at.fail_hdf5("cannot look up attribute '" + key + "'");
  }
  if (exists > 0 && H5Adelete(at.id, key.c_str()) < 0) {
    at.fail_hdf5("cannot replace attribute '" + key + "'");
  }
  Handle attribute{H5Acreate2(at.id, key.c_str(), file_type, space, H5P_DEFAULT, H5P_DEFAULT),
                   &H5Aclose};
  if (!attribute.valid()) {
    at.fail_hdf5("cannot create attribute '" + key + "'");
  }
  if (H5Awrite(attribute.id(), mem_type, data) < 0) {
    at.fail_hdf5("cannot write attribute '" + key + "'");
  }
  if (!attribute.close()) {
    at.fail_hdf5("cannot close attribute '" + key + "'");
  }
}

Handle open_attribute(const Where& at, const std::string& key) {
  const htri_t exists = H5Aexists(at.id, key.c_str());
  if (exists < 0) {
    at.fail_hdf5("cannot look up attribute '" + key + "'");
  }
  if (exists == 0) {
    at.fail("has no attribute '" + key + "'");
  }
  Handle attribute{H5Aopen(at.id, key.c_str(), H5P_DEFAULT), &H5Aclose};
  if (!attribute.valid()) {
    at.fail_hdf5("cannot open attribute '" + key + "'");
  }
  return attribute;
}

// Reads a scalar attribute whose stored type must equal expected_type exactly.
void read_scalar_attribute(const Where& at, std::string_view name, hid_t expected_type,
                           std::string_view expected_name, hid_t mem_type, void* out) {
  const std::string key{name};
  const Handle attribute = open_attribute(at, key);
  const Handle type{H5Aget_type(attribute.id()), &H5Tclose};
  const Handle space{H5Aget_space(attribute.id()), &H5Sclose};
  if (!type.valid() || !space.valid()) {
    at.fail_hdf5("cannot inspect attribute '" + key + "'");
  }
  const htri_t same = H5Tequal(type.id(), expected_type);
  if (same < 0) {
    at.fail_hdf5("cannot compare the type of attribute '" + key + "'");
  }
  if (same == 0 || H5Sget_simple_extent_type(space.id()) != H5S_SCALAR) {
    at.fail("attribute '" + key + "' is not a scalar " + std::string{expected_name});
  }
  if (H5Aread(attribute.id(), mem_type, out) < 0) {
    at.fail_hdf5("cannot read attribute '" + key + "'");
  }
}

// Owns the strings a variable-length read allocated into pointers: release() frees them and
// reports HDF5's result; the destructor frees them best effort if release() was not reached.
// Freeing a null entry is a no-op, so a partial or failed read is safe to release.
class VariableStrings {
 public:
  VariableStrings(hid_t type, hid_t space, std::vector<char*>& pointers) noexcept
      : type_(type), space_(space), pointers_(pointers) {}
  ~VariableStrings() { static_cast<void>(release()); }
  VariableStrings(const VariableStrings&) = delete;
  VariableStrings& operator=(const VariableStrings&) = delete;
  VariableStrings(VariableStrings&&) = delete;
  VariableStrings& operator=(VariableStrings&&) = delete;

  [[nodiscard]] bool release() noexcept {
    if (released_) {
      return true;
    }
    released_ = true;
    return H5Dvlen_reclaim(type_, space_, H5P_DEFAULT, string_buffer(pointers_.data())) >= 0;
  }

 private:
  hid_t type_;
  hid_t space_;
  std::vector<char*>& pointers_;
  bool released_ = false;
};

// Reads a variable-length string attribute: a scalar when want_scalar, else a one-dimensional
// array.
std::vector<std::string> read_string_attribute(const Where& at, std::string_view name,
                                               bool want_scalar) {
  const std::string key{name};
  const Handle attribute = open_attribute(at, key);
  const Handle type{H5Aget_type(attribute.id()), &H5Tclose};
  const Handle space{H5Aget_space(attribute.id()), &H5Sclose};
  if (!type.valid() || !space.valid()) {
    at.fail_hdf5("cannot inspect attribute '" + key + "'");
  }
  const bool is_string = H5Tget_class(type.id()) == H5T_STRING && H5Tis_variable_str(type.id()) > 0;
  const H5S_class_t extent = H5Sget_simple_extent_type(space.id());
  const bool is_scalar = extent == H5S_SCALAR;
  const bool is_array = extent == H5S_SIMPLE && H5Sget_simple_extent_ndims(space.id()) == 1;
  if (!is_string || (want_scalar ? !is_scalar : !is_array)) {
    at.fail("attribute '" + key + "' is not " +
            (want_scalar ? "a string" : "a one-dimensional string array"));
  }
  const hssize_t points = H5Sget_simple_extent_npoints(space.id());
  if (points < 0) {
    at.fail_hdf5("cannot size attribute '" + key + "'");
  }
  std::vector<std::string> values;
  if (points == 0) {
    return values;
  }
  const Handle string_type = variable_string_type(at);
  std::vector<char*> pointers(static_cast<std::size_t>(points), nullptr);
  // Armed before the read, so the strings HDF5 allocates are freed on every exit path, including
  // a std::bad_alloc from the copies below (Codex review F2).
  VariableStrings reclaim{string_type.id(), space.id(), pointers};
  if (H5Aread(attribute.id(), string_type.id(), string_buffer(pointers.data())) < 0) {
    at.fail_hdf5("cannot read attribute '" + key + "'");
  }
  values.reserve(pointers.size());
  for (const char* pointer : pointers) {
    values.emplace_back(pointer != nullptr ? pointer : "");
  }
  if (!reclaim.release()) {
    at.fail_hdf5("cannot release the strings read from attribute '" + key + "'");
  }
  return values;
}

void check_no_nul(const Where& at, std::string_view name, std::string_view value) {
  if (value.find('\0') != std::string_view::npos) {
    at.fail("string attribute '" + std::string{name} + "' contains a NUL character");
  }
}

}  // namespace

// --- Handle -------------------------------------------------------------------------------------

Handle::~Handle() { static_cast<void>(close()); }

Handle::Handle(Handle&& other) noexcept
    : id_(std::exchange(other.id_, -1)), close_(std::exchange(other.close_, nullptr)) {}

Handle& Handle::operator=(Handle&& other) noexcept {
  if (this != &other) {
    static_cast<void>(close());
    id_ = std::exchange(other.id_, -1);
    close_ = std::exchange(other.close_, nullptr);
  }
  return *this;
}

bool Handle::close() noexcept {
  if (id_ < 0) {
    return true;
  }
  const bool closed = close_(id_) >= 0;
  id_ = -1;
  close_ = nullptr;
  if (!closed) {
    note_hdf5_failure();
  }
  return closed;
}

// --- Object -------------------------------------------------------------------------------------

Object::Object(Handle handle, std::string file_path, std::string object_path) noexcept
    : handle_(std::move(handle)),
      file_path_(std::move(file_path)),
      object_path_(std::move(object_path)) {}

std::int64_t Object::id() const {
  if (!handle_.valid()) {
    fail(file_path_, object_path_, "the object is closed");
  }
  return handle_.id();
}

std::string Object::child_path(std::string_view name) const {
  std::string path = object_path_;
  if (path != "/") {
    path += '/';
  }
  path += name;
  return path;
}

void Object::close() {
  if (handle_.valid() && !handle_.close()) {
    fail_hdf5(file_path_, object_path_, "cannot close the object");
  }
}

void Object::set_attribute(std::string_view name, std::string_view value) {
  const Where at{id(), file_path_, object_path_};
  check_no_nul(at, name, value);
  const Handle type = variable_string_type(at);
  const Handle space = scalar_space(at);
  const std::string text{value};
  const char* pointer = text.c_str();
  write_attribute(at, name, type.id(), type.id(), space.id(), string_buffer(&pointer));
}

void Object::set_attribute(std::string_view name, std::span<const std::string> values) {
  const Where at{id(), file_path_, object_path_};
  std::vector<const char*> pointers;
  pointers.reserve(values.size());
  for (const std::string& value : values) {
    check_no_nul(at, name, value);
    pointers.push_back(value.c_str());
  }
  const Handle type = variable_string_type(at);
  const std::array<hsize_t, 1> dims{values.size()};
  const Handle space{H5Screate_simple(1, dims.data(), nullptr), &H5Sclose};
  if (!space.valid()) {
    at.fail_hdf5("cannot create the dataspace for attribute '" + std::string{name} + "'");
  }
  // H5Awrite refuses a null buffer even for zero elements, which an empty vector's data() is.
  const char* const none = nullptr;
  const void* data = string_buffer(pointers.empty() ? &none : pointers.data());
  write_attribute(at, name, type.id(), type.id(), space.id(), data);
}

void Object::set_attribute(std::string_view name, double value) {
  const Where at{id(), file_path_, object_path_};
  const Handle space = scalar_space(at);
  write_attribute(at, name, H5T_IEEE_F64LE, H5T_NATIVE_DOUBLE, space.id(), &value);
}

void Object::set_attribute(std::string_view name, std::int64_t value) {
  const Where at{id(), file_path_, object_path_};
  const Handle space = scalar_space(at);
  write_attribute(at, name, H5T_STD_I64LE, H5T_NATIVE_INT64, space.id(), &value);
}

void Object::set_attribute(std::string_view name, std::uint64_t value) {
  const Where at{id(), file_path_, object_path_};
  const Handle space = scalar_space(at);
  write_attribute(at, name, H5T_STD_U64LE, H5T_NATIVE_UINT64, space.id(), &value);
}

bool Object::has_attribute(std::string_view name) const {
  const Where at{id(), file_path_, object_path_};
  const std::string key{name};
  const htri_t exists = H5Aexists(at.id, key.c_str());
  if (exists < 0) {
    at.fail_hdf5("cannot look up attribute '" + key + "'");
  }
  return exists > 0;
}

std::string Object::attribute_string(std::string_view name) const {
  const Where at{id(), file_path_, object_path_};
  return std::move(read_string_attribute(at, name, true).front());
}

std::vector<std::string> Object::attribute_strings(std::string_view name) const {
  const Where at{id(), file_path_, object_path_};
  return read_string_attribute(at, name, false);
}

double Object::attribute_float64(std::string_view name) const {
  const Where at{id(), file_path_, object_path_};
  double value = 0.0;
  read_scalar_attribute(at, name, H5T_IEEE_F64LE, "float64", H5T_NATIVE_DOUBLE, &value);
  return value;
}

std::int64_t Object::attribute_int64(std::string_view name) const {
  const Where at{id(), file_path_, object_path_};
  std::int64_t value = 0;
  read_scalar_attribute(at, name, H5T_STD_I64LE, "int64", H5T_NATIVE_INT64, &value);
  return value;
}

std::uint64_t Object::attribute_uint64(std::string_view name) const {
  const Where at{id(), file_path_, object_path_};
  std::uint64_t value = 0;
  read_scalar_attribute(at, name, H5T_STD_U64LE, "uint64", H5T_NATIVE_UINT64, &value);
  return value;
}

// --- Location -----------------------------------------------------------------------------------

Group Location::create_group(std::string_view name) {
  const Where at{id(), file_path(), object_path()};
  const std::string key{name};
  std::string path = child_path(name);
  const Handle properties{H5Pcreate(H5P_GROUP_CREATE), &H5Pclose};
  if (!properties.valid() || H5Pset_obj_track_times(properties.id(), false) < 0) {
    fail_hdf5(file_path(), path, "cannot build the group creation properties");
  }
  Handle group{H5Gcreate2(at.id, key.c_str(), H5P_DEFAULT, properties.id(), H5P_DEFAULT),
               &H5Gclose};
  if (!group.valid()) {
    fail_hdf5(file_path(), path, "cannot create the group");
  }
  return Group{std::move(group), file_path(), std::move(path)};
}

Group Location::open_group(std::string_view name) const {
  const Where at{id(), file_path(), object_path()};
  const std::string key{name};
  std::string path = child_path(name);
  Handle group{H5Gopen2(at.id, key.c_str(), H5P_DEFAULT), &H5Gclose};
  if (!group.valid()) {
    fail_hdf5(file_path(), path, "cannot open the group");
  }
  return Group{std::move(group), file_path(), std::move(path)};
}

// --- Compound record tables ---------------------------------------------------------------------

std::size_t RecordField::rows() const noexcept {
  if (const auto* numbers = std::get_if<std::vector<double>>(&values)) {
    return numbers->size();
  }
  if (const auto* counts = std::get_if<std::vector<std::uint64_t>>(&values)) {
    return counts->size();
  }
  const auto* strings = std::get_if<std::vector<std::string>>(&values);
  return strings != nullptr ? strings->size() : 0;
}

namespace {

// Every member is eight bytes in memory: a double, a uint64 or a char* (a variable-length string).
constexpr std::size_t kRecordMemberBytes = 8;
static_assert(sizeof(double) == kRecordMemberBytes && sizeof(std::uint64_t) == kRecordMemberBytes &&
              sizeof(char*) == kRecordMemberBytes);

enum class MemberKind : std::uint8_t { kFloat64, kUint64, kString };

MemberKind member_kind(const RecordField& field) noexcept {
  if (std::holds_alternative<std::vector<double>>(field.values)) {
    return MemberKind::kFloat64;
  }
  if (std::holds_alternative<std::vector<std::uint64_t>>(field.values)) {
    return MemberKind::kUint64;
  }
  return MemberKind::kString;
}

// The compound type for these member kinds: `file` picks the little-endian stored types, else the
// native memory types; the string member is the same variable-length UTF-8 type either way.
Handle compound_type(const Where& at, std::span<const MemberKind> kinds,
                     std::span<const std::string> names, bool file) {
  Handle type{H5Tcreate(H5T_COMPOUND, kinds.size() * kRecordMemberBytes), &H5Tclose};
  if (!type.valid()) {
    at.fail_hdf5("cannot build the record type");
  }
  const Handle string_type = variable_string_type(at);
  for (std::size_t i = 0; i < kinds.size(); ++i) {
    hid_t member = string_type.id();
    if (kinds[i] == MemberKind::kFloat64) {
      member = file ? H5T_IEEE_F64LE : H5T_NATIVE_DOUBLE;
    } else if (kinds[i] == MemberKind::kUint64) {
      member = file ? H5T_STD_U64LE : H5T_NATIVE_UINT64;
    }
    if (H5Tinsert(type.id(), names[i].c_str(), i * kRecordMemberBytes, member) < 0) {
      at.fail_hdf5("cannot add record member '" + names[i] + "'");
    }
  }
  return type;
}

// Frees the strings a variable-length read allocated inside a compound buffer: release() reports
// HDF5's result; the destructor frees best effort if release() was not reached.
class ReclaimRecords {
 public:
  ReclaimRecords(hid_t type, hid_t space, std::vector<std::byte>& buffer) noexcept
      : type_(type), space_(space), buffer_(buffer) {}
  ~ReclaimRecords() { static_cast<void>(release()); }
  ReclaimRecords(const ReclaimRecords&) = delete;
  ReclaimRecords& operator=(const ReclaimRecords&) = delete;
  ReclaimRecords(ReclaimRecords&&) = delete;
  ReclaimRecords& operator=(ReclaimRecords&&) = delete;

  [[nodiscard]] bool release() noexcept {
    if (released_) {
      return true;
    }
    released_ = true;
    return H5Dvlen_reclaim(type_, space_, H5P_DEFAULT, buffer_.data()) >= 0;
  }

 private:
  hid_t type_;
  hid_t space_;
  std::vector<std::byte>& buffer_;
  bool released_ = false;
};

// A record table's layout: each member's kind and name, and the row count.
struct RecordLayout {
  std::vector<MemberKind> kinds;
  std::vector<std::string> names;
  std::size_t rows = 0;
};

// The layout of fields about to be written, checked (see write_records' contract).
RecordLayout checked_layout(const Where& target, std::span<const RecordField> fields) {
  if (fields.empty()) {
    target.fail("a record table needs at least one field");
  }
  RecordLayout layout;
  layout.rows = fields.front().rows();
  for (const RecordField& field : fields) {
    if (field.name.empty() ||
        std::find(layout.names.begin(), layout.names.end(), field.name) != layout.names.end()) {
      target.fail("record field names must be non-empty and distinct ('" + field.name + "')");
    }
    if (field.rows() != layout.rows) {
      target.fail("record field '" + field.name + "' has " + std::to_string(field.rows()) +
                  " rows, not " + std::to_string(layout.rows));
    }
    if (const auto* strings = std::get_if<std::vector<std::string>>(&field.values)) {
      for (const std::string& value : *strings) {
        check_no_nul(target, field.name, value);
      }
    }
    layout.kinds.push_back(member_kind(field));
    layout.names.push_back(field.name);
  }
  return layout;
}

// Row-major, one eight-byte slot per member; a string member holds a pointer into `fields`, so
// the buffer is valid only while they are.
std::vector<std::byte> pack_records(std::span<const RecordField> fields, std::size_t rows) {
  const std::size_t row_bytes = fields.size() * kRecordMemberBytes;
  std::vector<std::byte> buffer(rows * row_bytes);
  for (std::size_t f = 0; f < fields.size(); ++f) {
    const std::size_t offset = f * kRecordMemberBytes;
    std::visit(
        [&buffer, row_bytes, offset](const auto& column) {
          for (std::size_t r = 0; r < column.size(); ++r) {
            std::byte* slot = buffer.data() + (r * row_bytes) + offset;
            if constexpr (std::is_same_v<std::decay_t<decltype(column)>,
                                         std::vector<std::string>>) {
              const char* text = column[r].c_str();
              std::memcpy(slot, string_buffer(&text), kRecordMemberBytes);
            } else {
              std::memcpy(slot, &column[r], kRecordMemberBytes);
            }
          }
        },
        fields[f].values);
  }
  return buffer;
}

MemberKind stored_member_kind(const Where& target, hid_t stored, unsigned index,
                              const std::string& name) {
  switch (H5Tget_member_class(stored, index)) {
    case H5T_FLOAT:
      return MemberKind::kFloat64;
    case H5T_INTEGER:
      return MemberKind::kUint64;
    case H5T_STRING:
      return MemberKind::kString;
    default:
      target.fail("record member '" + name + "' is not float64, uint64 or string");
  }
}

// The layout of a stored compound dataset, which must be one-dimensional.
RecordLayout stored_layout(const Where& target, hid_t stored, hid_t space) {
  if (H5Tget_class(stored) != H5T_COMPOUND || H5Sget_simple_extent_ndims(space) != 1) {
    target.fail("is not a one-dimensional compound dataset");
  }
  const int members = H5Tget_nmembers(stored);
  const hssize_t points = H5Sget_simple_extent_npoints(space);
  if (members <= 0 || points < 0) {
    target.fail_hdf5("cannot size the record table");
  }
  RecordLayout layout;
  layout.rows = static_cast<std::size_t>(points);
  for (unsigned index = 0; index < static_cast<unsigned>(members); ++index) {
    char* member_name = H5Tget_member_name(stored, index);
    if (member_name == nullptr) {
      target.fail_hdf5("cannot read a record member name");
    }
    layout.names.emplace_back(member_name);
    static_cast<void>(H5free_memory(member_name));
    layout.kinds.push_back(stored_member_kind(target, stored, index, layout.names.back()));
  }
  return layout;
}

// One member's column out of a buffer H5Dread filled.
template <typename T>
std::vector<T> unpack_numbers(const std::vector<std::byte>& buffer, std::size_t rows,
                              std::size_t row_bytes, std::size_t offset) {
  std::vector<T> column(rows);
  for (std::size_t r = 0; r < rows; ++r) {
    std::memcpy(&column[r], buffer.data() + (r * row_bytes) + offset, kRecordMemberBytes);
  }
  return column;
}

std::vector<std::string> unpack_strings(const std::vector<std::byte>& buffer, std::size_t rows,
                                        std::size_t row_bytes, std::size_t offset) {
  std::vector<std::string> column(rows);
  for (std::size_t r = 0; r < rows; ++r) {
    char* text = nullptr;
    std::memcpy(string_buffer(&text), buffer.data() + (r * row_bytes) + offset, kRecordMemberBytes);
    column[r] = text != nullptr ? text : "";
  }
  return column;
}

RecordField unpack_field(const RecordLayout& layout, std::size_t f,
                         const std::vector<std::byte>& buffer) {
  const std::size_t row_bytes = layout.kinds.size() * kRecordMemberBytes;
  const std::size_t offset = f * kRecordMemberBytes;
  RecordField field{layout.names[f], std::vector<double>{}};
  if (layout.kinds[f] == MemberKind::kFloat64) {
    field.values = unpack_numbers<double>(buffer, layout.rows, row_bytes, offset);
  } else if (layout.kinds[f] == MemberKind::kUint64) {
    field.values = unpack_numbers<std::uint64_t>(buffer, layout.rows, row_bytes, offset);
  } else {
    field.values = unpack_strings(buffer, layout.rows, row_bytes, offset);
  }
  return field;
}

}  // namespace

void Location::write_records(std::string_view name, std::span<const RecordField> fields) {
  const Where at{id(), file_path(), object_path()};
  const std::string key{name};
  const std::string path = child_path(name);
  const Where target{at.id, file_path(), path};
  const RecordLayout layout = checked_layout(target, fields);
  const std::vector<std::byte> buffer = pack_records(fields, layout.rows);
  const Handle file_type = compound_type(target, layout.kinds, layout.names, true);
  const Handle memory_type = compound_type(target, layout.kinds, layout.names, false);
  const std::array<hsize_t, 1> dims{layout.rows};
  const Handle space{H5Screate_simple(1, dims.data(), nullptr), &H5Sclose};
  const Handle properties{H5Pcreate(H5P_DATASET_CREATE), &H5Pclose};
  if (!space.valid() || !properties.valid() || H5Pset_obj_track_times(properties.id(), false) < 0) {
    target.fail_hdf5("cannot build the record table's dataspace and creation properties");
  }
  const Handle dataset{H5Dcreate2(at.id, key.c_str(), file_type.id(), space.id(), H5P_DEFAULT,
                                  properties.id(), H5P_DEFAULT),
                       &H5Dclose};
  if (!dataset.valid()) {
    target.fail_hdf5("cannot create the record table");
  }
  if (layout.rows != 0 &&
      H5Dwrite(dataset.id(), memory_type.id(), H5S_ALL, H5S_ALL, H5P_DEFAULT, buffer.data()) < 0) {
    target.fail_hdf5("cannot write the record table");
  }
}

std::vector<RecordField> Location::read_records(std::string_view name) const {
  const Where at{id(), file_path(), object_path()};
  const std::string key{name};
  const std::string path = child_path(name);
  const Where target{at.id, file_path(), path};
  const Handle dataset{H5Dopen2(at.id, key.c_str(), H5P_DEFAULT), &H5Dclose};
  if (!dataset.valid()) {
    target.fail_hdf5("cannot open the record table");
  }
  const Handle stored{H5Dget_type(dataset.id()), &H5Tclose};
  const Handle space{H5Dget_space(dataset.id()), &H5Sclose};
  if (!stored.valid() || !space.valid()) {
    target.fail_hdf5("cannot inspect the record table");
  }
  const RecordLayout layout = stored_layout(target, stored.id(), space.id());
  const Handle memory_type = compound_type(target, layout.kinds, layout.names, false);
  std::vector<std::byte> buffer(
      std::max<std::size_t>(layout.rows * layout.kinds.size() * kRecordMemberBytes, 1));
  // Armed before the read, so the strings HDF5 allocates are freed on every exit path.
  std::optional<ReclaimRecords> reclaim;
  if (layout.rows != 0) {
    reclaim.emplace(memory_type.id(), space.id(), buffer);
    if (H5Dread(dataset.id(), memory_type.id(), H5S_ALL, H5S_ALL, H5P_DEFAULT, buffer.data()) < 0) {
      target.fail_hdf5("cannot read the record table");
    }
  }
  std::vector<RecordField> fields;
  fields.reserve(layout.kinds.size());
  for (std::size_t f = 0; f < layout.kinds.size(); ++f) {
    fields.push_back(unpack_field(layout, f, buffer));
  }
  if (reclaim.has_value() && !reclaim->release()) {
    target.fail_hdf5("cannot release the strings read from the record table");
  }
  return fields;
}

namespace {

// An empty, chunked, unlimited-row dataset of rank 1 (columns must be 1) or 2.
Handle create_appendable(const Where& at, const std::string& path, std::string_view name,
                         ElementType type, int rank, std::size_t columns, std::size_t chunk_rows) {
  if (columns == 0) {
    fail(at.file, path, "a table needs at least one column");
  }
  if (chunk_rows == 0) {
    fail(at.file, path, "chunk_rows must be at least 1");
  }
  const std::array<hsize_t, 2> dims{0, columns};
  const std::array<hsize_t, 2> max_dims{H5S_UNLIMITED, columns};
  const std::array<hsize_t, 2> chunk{chunk_rows, columns};
  const Handle space{H5Screate_simple(rank, dims.data(), max_dims.data()), &H5Sclose};
  const Handle properties{H5Pcreate(H5P_DATASET_CREATE), &H5Pclose};
  if (!space.valid() || !properties.valid() ||
      H5Pset_chunk(properties.id(), rank, chunk.data()) < 0 ||
      H5Pset_obj_track_times(properties.id(), false) < 0) {
    fail_hdf5(at.file, path, "cannot build the dataset's dataspace and creation properties");
  }
  const std::string key{name};
  Handle dataset{H5Dcreate2(at.id, key.c_str(), stored_type(type), space.id(), H5P_DEFAULT,
                            properties.id(), H5P_DEFAULT),
                 &H5Dclose};
  if (!dataset.valid()) {
    fail_hdf5(at.file, path, "cannot create the dataset");
  }
  return dataset;
}

}  // namespace

Dataset Location::create_series(std::string_view name, ElementType type, std::size_t chunk_rows) {
  const Where at{id(), file_path(), object_path()};
  std::string path = child_path(name);
  Handle dataset = create_appendable(at, path, name, type, 1, 1, chunk_rows);
  return Dataset{std::move(dataset), file_path(), std::move(path), type, 1, 1};
}

Dataset Location::create_table(std::string_view name, ElementType type, std::size_t columns,
                               std::size_t chunk_rows) {
  const Where at{id(), file_path(), object_path()};
  std::string path = child_path(name);
  Handle dataset = create_appendable(at, path, name, type, 2, columns, chunk_rows);
  return Dataset{std::move(dataset), file_path(), std::move(path), type, 2, columns};
}

Dataset Location::open_dataset(std::string_view name) const {
  const Where at{id(), file_path(), object_path()};
  const std::string key{name};
  std::string path = child_path(name);
  Handle dataset{H5Dopen2(at.id, key.c_str(), H5P_DEFAULT), &H5Dclose};
  if (!dataset.valid()) {
    fail_hdf5(file_path(), path, "cannot open the dataset");
  }
  const Handle type{H5Dget_type(dataset.id()), &H5Tclose};
  const Handle space{H5Dget_space(dataset.id()), &H5Sclose};
  if (!type.valid() || !space.valid()) {
    fail_hdf5(file_path(), path, "cannot inspect the dataset");
  }
  ElementType element = ElementType::kFloat64;
  if (H5Tequal(type.id(), H5T_IEEE_F64LE) > 0) {
    element = ElementType::kFloat64;
  } else if (H5Tequal(type.id(), H5T_STD_U64LE) > 0) {
    element = ElementType::kUint64;
  } else {
    fail(file_path(), path, "the dataset's element type is neither float64 nor uint64");
  }
  const int rank = H5Sget_simple_extent_ndims(space.id());
  if (rank != 1 && rank != 2) {
    fail(file_path(), path, "the dataset is not one- or two-dimensional");
  }
  std::array<hsize_t, 2> dims{0, 1};
  if (H5Sget_simple_extent_dims(space.id(), dims.data(), nullptr) < 0) {
    fail_hdf5(file_path(), path, "cannot read the dataset's extent");
  }
  const std::size_t columns = rank == 2 ? dims[1] : 1;
  return Dataset{std::move(dataset), file_path(), std::move(path), element, rank, columns};
}

// --- Dataset ------------------------------------------------------------------------------------

Dataset::Dataset(Handle handle, std::string file_path, std::string object_path, ElementType type,
                 int rank, std::size_t columns) noexcept
    : Object(std::move(handle), std::move(file_path), std::move(object_path)),
      type_(type),
      rank_(rank),
      columns_(columns) {}

std::size_t Dataset::rows() const {
  const Where at{id(), file_path(), object_path()};
  const Handle space{H5Dget_space(at.id), &H5Sclose};
  std::array<hsize_t, 2> dims{0, 0};
  if (!space.valid() || H5Sget_simple_extent_dims(space.id(), dims.data(), nullptr) < 0) {
    at.fail_hdf5("cannot read the dataset's extent");
  }
  return dims[0];
}

void Dataset::append(std::span<const double> values) {
  append_rows(values.data(), values.size(), ElementType::kFloat64);
}

void Dataset::append(std::span<const std::uint64_t> values) {
  append_rows(values.data(), values.size(), ElementType::kUint64);
}

void Dataset::append_rows(const void* values, std::size_t count, ElementType type) {
  const Where at{id(), file_path(), object_path()};
  if (type != type_) {
    at.fail("cannot append " + std::string{type_name(type)} + " values to a " +
            std::string{type_name(type_)} + " dataset");
  }
  if (count == 0) {
    return;
  }
  if (count % columns_ != 0) {
    at.fail(std::to_string(count) + " values are not a whole number of " +
            std::to_string(columns_) + "-column rows");
  }
  const std::size_t before = rows();
  const hsize_t added = count / columns_;
  const std::array<hsize_t, 2> grown{before + added, columns_};
  if (H5Dset_extent(at.id, grown.data()) < 0) {
    at.fail_hdf5("cannot extend the dataset");
  }
  // From here a failure shrinks the extent back, so a refused append leaves no fill rows.
  const auto undo_and_fail = [&at, before, added, this](std::string_view message) {
    note_hdf5_failure();
    std::string text{message};
    text += " (";
    text += take_error_stack();
    text += ")";
    const std::array<hsize_t, 2> restored{before, columns_};
    if (H5Dset_extent(at.id, restored.data()) < 0) {
      // Codex review F1: the rollback is checked, and its failure is reported, never hidden.
      text += "; restoring the extent to " + std::to_string(before) +
              " rows also failed, so the dataset may now end in " + std::to_string(added) +
              " unwritten fill rows (" + take_error_stack() + ")";
    }
    at.fail(text);
  };
  const Handle file_space{H5Dget_space(at.id), &H5Sclose};
  const std::array<hsize_t, 2> start{before, 0};
  const std::array<hsize_t, 2> block{added, columns_};
  if (!file_space.valid() || H5Sselect_hyperslab(file_space.id(), H5S_SELECT_SET, start.data(),
                                                 nullptr, block.data(), nullptr) < 0) {
    undo_and_fail("cannot select the appended rows");
  }
  const Handle memory_space{H5Screate_simple(rank_, block.data(), nullptr), &H5Sclose};
  if (!memory_space.valid()) {
    undo_and_fail("cannot create the memory dataspace");
  }
  if (H5Dwrite(at.id, memory_type(type), memory_space.id(), file_space.id(), H5P_DEFAULT, values) <
      0) {
    undo_and_fail("cannot write the appended rows");
  }
}

void Dataset::read_all(void* out, std::size_t count, ElementType type) const {
  const Where at{id(), file_path(), object_path()};
  if (type != type_) {
    at.fail("cannot read a " + std::string{type_name(type_)} + " dataset as " +
            std::string{type_name(type)});
  }
  if (count != 0 && H5Dread(at.id, memory_type(type), H5S_ALL, H5S_ALL, H5P_DEFAULT, out) < 0) {
    at.fail_hdf5("cannot read the dataset");
  }
}

std::vector<double> Dataset::read_float64() const {
  std::vector<double> values(rows() * columns_);
  read_all(values.data(), values.size(), ElementType::kFloat64);
  return values;
}

std::vector<std::uint64_t> Dataset::read_uint64() const {
  std::vector<std::uint64_t> values(rows() * columns_);
  read_all(values.data(), values.size(), ElementType::kUint64);
  return values;
}

// --- File ---------------------------------------------------------------------------------------

File File::create(const std::filesystem::path& path) {
  std::string file = path.string();
  if (!kOwnsTeardown) {
    fail(file, "",
         "refusing to write: libhdf5 was initialized before libastra_core was loaded, so its own "
         "exit hook stays installed and a failed write could crash this process at exit "
         "(libhdf5 1.10.10; see core/src/signals/hdf5_file.cpp)");
  }
  silence_error_printing();
  const Handle access{H5Pcreate(H5P_FILE_ACCESS), &H5Pclose};
  const Handle creation{H5Pcreate(H5P_FILE_CREATE), &H5Pclose};
  if (!access.valid() || !creation.valid() ||
      H5Pset_libver_bounds(access.id(), H5F_LIBVER_V18, H5F_LIBVER_V110) < 0 ||
      H5Pset_fclose_degree(access.id(), H5F_CLOSE_WEAK) < 0 ||
      H5Pset_obj_track_times(creation.id(), false) < 0) {
    fail_hdf5(file, "", "cannot build the file's creation and access properties");
  }
  Handle handle{H5Fcreate(file.c_str(), H5F_ACC_EXCL, creation.id(), access.id()), &H5Fclose};
  if (!handle.valid()) {
    fail_hdf5(file, "",
              "cannot create the file (it must not already exist, and its directory must exist "
              "and be writable)");
  }
  return File{std::move(handle), std::move(file), "/"};
}

File File::open_read_only(const std::filesystem::path& path) {
  silence_error_printing();
  std::string file = path.string();
  const Handle access{H5Pcreate(H5P_FILE_ACCESS), &H5Pclose};
  if (!access.valid() || H5Pset_fclose_degree(access.id(), H5F_CLOSE_WEAK) < 0) {
    fail_hdf5(file, "", "cannot build the file's access properties");
  }
  Handle handle{H5Fopen(file.c_str(), H5F_ACC_RDONLY, access.id()), &H5Fclose};
  if (!handle.valid()) {
    fail_hdf5(file, "", "cannot open the file for reading");
  }
  return File{std::move(handle), std::move(file), "/"};
}

void File::flush() {
  const Where at{id(), file_path(), object_path()};
  if (H5Fflush(at.id, H5F_SCOPE_LOCAL) < 0) {
    at.fail_hdf5("cannot flush the file");
  }
}

void File::close() {
  if (!is_open()) {
    return;
  }
  const Where at{id(), file_path(), object_path()};
  // Counts this file identifier itself too, hence "more than one".
  const ssize_t open_objects = H5Fget_obj_count(at.id, H5F_OBJ_ALL | H5F_OBJ_LOCAL);
  if (open_objects < 0) {
    at.fail_hdf5("cannot count the file's open objects");
  }
  if (open_objects > 1) {
    at.fail(std::to_string(open_objects - 1) +
            " group, dataset or attribute handle(s) opened from the file are still open; close "
            "them before the file");
  }
  Object::close();
}

unsigned File::superblock_version() const {
  const Where at{id(), file_path(), object_path()};
  H5F_info2_t info{};
  if (H5Fget_info2(at.id, &info) < 0) {
    at.fail_hdf5("cannot read the file's superblock information");
  }
  return info.super.version;
}

}  // namespace astra::hdf5
