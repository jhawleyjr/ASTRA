// The HDF5 RAII wrapper (ADR-0005, ADR-0025): the only ASTRA code that calls the HDF5 C API, and
// the only code that includes <hdf5.h> (its source, not this header). It owns every HDF5
// identifier, turns every HDF5 failure into an Hdf5Error naming the file and object, and fixes the
// file settings ADR-0025's byte-identity criterion (Q7) relies on:
//   - object time tracking off on the root group, every group and every dataset, so no creation
//     or modification time is written;
//   - library-version bounds fixed at (1.8, 1.10) by explicit enum, never EARLIEST/LATEST, so a
//     newer libhdf5 does not silently change the format. The low bound 1.8 keeps superblock
//     version 2: a 1.10 low bound gives version 3, whose file-consistency flag makes a file left
//     unclosed by a crash unopenable without h5clear (observed with libhdf5 1.10.10).
// Two identical sequences of calls to different paths give byte-identical files
// (tests/unit/signals/hdf5_file_test.cpp, Hdf5Determinism).
//
// Not the per-frame path: every call here may allocate and throw. ADR-0025's logger calls this
// only at init, in a drain between frames, and at close_log().
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace astra::hdf5 {

// The stored element types: IEEE float64 and unsigned 64-bit integers, both little-endian.
enum class ElementType : std::uint8_t { kFloat64, kUint64 };

// Owns one HDF5 identifier and releases it with the H5*close function it was opened with. The
// destructor is best effort: a failed close is ignored (HDF5's own error printing is off).
class Handle {
 public:
  // herr_t (*)(hid_t) in HDF5 1.10; the source static_asserts that hid_t is std::int64_t.
  using CloseFunction = int (*)(std::int64_t);

  Handle() noexcept = default;
  Handle(std::int64_t id, CloseFunction close) noexcept : id_(id), close_(close) {}
  ~Handle();
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept;
  Handle& operator=(Handle&& other) noexcept;

  [[nodiscard]] std::int64_t id() const noexcept { return id_; }
  [[nodiscard]] bool valid() const noexcept { return id_ >= 0; }

  // Closes now and reports whether HDF5 accepted the close. The handle is empty afterwards
  // either way; closing an empty handle succeeds.
  [[nodiscard]] bool close() noexcept;

 private:
  std::int64_t id_ = -1;
  CloseFunction close_ = nullptr;
};

// Anything that carries attributes: a file's root group, a group or a dataset. Setting an
// attribute that exists replaces it (type and shape may change). Strings are stored as
// variable-length UTF-8. Every getter throws Hdf5Error on a missing attribute or on a stored type
// other than the one the getter reads.
class Object {
 public:
  void set_attribute(std::string_view name, std::string_view value);
  void set_attribute(std::string_view name, std::span<const std::string> values);
  void set_attribute(std::string_view name, double value);
  void set_attribute(std::string_view name, std::int64_t value);
  void set_attribute(std::string_view name, std::uint64_t value);

  [[nodiscard]] bool has_attribute(std::string_view name) const;
  [[nodiscard]] std::string attribute_string(std::string_view name) const;
  [[nodiscard]] std::vector<std::string> attribute_strings(std::string_view name) const;
  [[nodiscard]] double attribute_float64(std::string_view name) const;
  [[nodiscard]] std::int64_t attribute_int64(std::string_view name) const;
  [[nodiscard]] std::uint64_t attribute_uint64(std::string_view name) const;

  // Closes the object now; throws Hdf5Error if HDF5 refuses. Closing a closed object does
  // nothing. Every other call on a closed object throws.
  void close();
  [[nodiscard]] bool is_open() const noexcept { return handle_.valid(); }

  // For error messages: the file on disk and the object's absolute path inside it ("/rate_1").
  [[nodiscard]] const std::string& file_path() const noexcept { return file_path_; }
  [[nodiscard]] const std::string& object_path() const noexcept { return object_path_; }

 protected:
  Object(Handle handle, std::string file_path, std::string object_path) noexcept;

  // The open identifier; throws Hdf5Error naming this object if it is closed.
  [[nodiscard]] std::int64_t id() const;
  [[nodiscard]] std::string child_path(std::string_view name) const;

 private:
  Handle handle_;
  std::string file_path_;
  std::string object_path_;
};

class Group;
class Dataset;

// A file's root group or a group: creates and opens groups and datasets by name.
class Location : public Object {
 public:
  // Throws Hdf5Error if the name exists.
  Group create_group(std::string_view name);
  [[nodiscard]] Group open_group(std::string_view name) const;

  // Appendable chunked datasets, empty at creation, uncompressed. A series is one-dimensional
  // (one value per row: /rate_<d>/frame); a table is rows x columns (/rate_<d>/values).
  // chunk_rows is the rows per chunk; zero columns or zero chunk_rows throws.
  Dataset create_series(std::string_view name, ElementType type, std::size_t chunk_rows);
  Dataset create_table(std::string_view name, ElementType type, std::size_t columns,
                       std::size_t chunk_rows);
  [[nodiscard]] Dataset open_dataset(std::string_view name) const;

 protected:
  Location(Handle handle, std::string file_path, std::string object_path) noexcept
      : Object(std::move(handle), std::move(file_path), std::move(object_path)) {}
};

class Group : public Location {
 private:
  friend class Location;
  Group(Handle handle, std::string file_path, std::string object_path) noexcept
      : Location(std::move(handle), std::move(file_path), std::move(object_path)) {}
};

// A one- or two-dimensional dataset of one element type that grows by whole rows. The wrapper
// only opens datasets of the two element types and ranks it creates.
class Dataset : public Object {
 public:
  [[nodiscard]] ElementType type() const noexcept { return type_; }
  // 1 for a series, 2 for a table.
  [[nodiscard]] int rank() const noexcept { return rank_; }
  // 1 for a series.
  [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
  [[nodiscard]] std::size_t rows() const;

  // Appends values.size() / columns() whole rows, row-major. Throws Hdf5Error, leaving the
  // dataset's extent as it was, on a type that is not this dataset's, a size that is not a
  // whole number of rows, or a failed HDF5 write. An empty span appends nothing.
  void append(std::span<const double> values);
  void append(std::span<const std::uint64_t> values);

  // Every row, row-major. Throws Hdf5Error if the dataset's type is not the one read.
  [[nodiscard]] std::vector<double> read_float64() const;
  [[nodiscard]] std::vector<std::uint64_t> read_uint64() const;

 private:
  friend class Location;
  Dataset(Handle handle, std::string file_path, std::string object_path, ElementType type, int rank,
          std::size_t columns) noexcept;

  void append_rows(const void* values, std::size_t count, ElementType type);
  void read_all(void* out, std::size_t count, ElementType type) const;

  ElementType type_;
  int rank_;
  std::size_t columns_;
};

// An HDF5 file. create() never overwrites (ADR-0025 Q5) and applies the fixed settings above.
class File : public Location {
 public:
  // Creates path, which must not exist. Throws Hdf5Error naming path if it exists, its directory
  // is missing or unwritable, or HDF5 refuses.
  static File create(const std::filesystem::path& path);
  // Opens an existing file read-only; throws Hdf5Error naming path on any failure.
  static File open_read_only(const std::filesystem::path& path);

  // Writes HDF5's buffered data and metadata to disk; the file stays open.
  void flush();

  // Closes the file. Throws Hdf5Error, leaving the file open, if any group, dataset or attribute
  // opened from it is still open, and Hdf5Error if HDF5 fails to close it. Idempotent. The
  // destructor instead closes best effort, and the file then closes when its last object does.
  void close();

  // The superblock version on disk: 2 for every file create() writes (the 1.8 low bound).
  [[nodiscard]] unsigned superblock_version() const;

 private:
  File(Handle handle, std::string file_path, std::string object_path) noexcept
      : Location(std::move(handle), std::move(file_path), std::move(object_path)) {}
};

}  // namespace astra::hdf5
