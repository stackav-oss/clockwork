// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/uuid/uuid.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clockwork::serialization
{

/// Exception thrown when a clockwork type fails to upgrade
class ClkTypeUpgradeError : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

/// Exception thrown when a clockwork schema field fails to upgrade
class ClkSchemaFieldUpgradeError : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

/// Clockwork type identifiers
WISE_ENUM_CLASS(
  (ClkTypeId, uint8_t),
  // Bool
  boolean,
  // Int8
  int8,
  // Int16
  int16,
  // Int32
  int32,
  // Int64
  int64,
  // UInt8
  uint8,
  // Uint16
  uint16,
  // UInt32
  uint32,
  // UInt64
  uint64,
  // Float32
  float32,
  // Float64
  float64,
  // SyncTime
  synctime,
  // Duration
  duration,
  // Byte
  byte,
  // UUID
  uuid,
  // Tag
  tag,
  // Fixed array
  fixed_array,
  // Variable array
  var_array,
  // Variable string
  var_string,
  // Optional
  optional,
  // Fixed SoA (Struct-of-Arrays)
  fixed_soa,
  // Variable SoA (Struct-of-Arrays)
  var_soa,
  // Enum
  clk_enum,
  // Schema
  schema,
  // Tensor
  tensor,
  // Bitset
  bitset)

/// Schema UUID type
using SchemaUuid = jewels::Uuid<int8_t>;

/// Enum UUID type
using EnumUuid = jewels::Uuid<uint8_t>;

/// Clockwork value initializer interface
class ClkValueInitializer
{
public:
  ClkValueInitializer() noexcept = default;

  /// Virtual destructor
  virtual ~ClkValueInitializer() noexcept = default;

  ClkValueInitializer(const ClkValueInitializer&) = delete;
  ClkValueInitializer& operator=(const ClkValueInitializer&) = delete;
  ClkValueInitializer(ClkValueInitializer&&) = delete;
  ClkValueInitializer& operator=(ClkValueInitializer&&) = delete;

  /// Initialize a clockwork value
  /// @param[in] dest_span Storage to be initialized
  virtual void initialize(std::span<std::byte> dest_span) const = 0;
};

/// Clockwork type upgrader interface
class ClkTypeUpgrader
{
public:
  ClkTypeUpgrader() noexcept = default;

  /// Virtual destructor
  virtual ~ClkTypeUpgrader() noexcept = default;

  ClkTypeUpgrader(const ClkTypeUpgrader&) = delete;
  ClkTypeUpgrader& operator=(const ClkTypeUpgrader&) = delete;
  ClkTypeUpgrader(ClkTypeUpgrader&&) = delete;
  ClkTypeUpgrader& operator=(ClkTypeUpgrader&&) = delete;

  /// Upgrade a clockwork type instance to the current schema
  /// @param[in] src_span Storage for the instance to upgrade from
  /// @param[in] dest_span Storage for the instance to upgrade to
  virtual void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const = 0;
};

/// Clockwork memcpy upgrader
class ClkMemcpyUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  explicit ClkMemcpyUpgrader(size_t size) noexcept;

  ~ClkMemcpyUpgrader() noexcept override = default;

  ClkMemcpyUpgrader(const ClkMemcpyUpgrader&) = delete;
  ClkMemcpyUpgrader& operator=(const ClkMemcpyUpgrader&) = delete;
  ClkMemcpyUpgrader(ClkMemcpyUpgrader&&) = delete;
  ClkMemcpyUpgrader& operator=(ClkMemcpyUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Number of bytes to copy
  size_t size_;
};

/// Validate array sizes for array upgrade
/// @param[in] array_size Array size
/// @param[in] min_array_size Minimum array size
/// @param[in] max_array_size Maximum array size
/// @throws runtime_error if array size is invalid
void validate_array_size(size_t array_size, size_t min_array_size, size_t max_array_size);

/// Clockwork array upgrader interface
class ClkArrayUpgrader
{
public:
  ClkArrayUpgrader() noexcept = default;

  /// Virtual destructor
  virtual ~ClkArrayUpgrader() noexcept = default;

  ClkArrayUpgrader(const ClkArrayUpgrader&) = delete;
  ClkArrayUpgrader& operator=(const ClkArrayUpgrader&) = delete;
  ClkArrayUpgrader(ClkArrayUpgrader&&) = delete;
  ClkArrayUpgrader& operator=(ClkArrayUpgrader&&) = delete;

  /// Upgrade an array of clockwork type instances to the current schema
  /// @param[in] src_array_size Source array size
  /// @param[in] src_span Storage for the instance to upgrade from
  /// @param[in] dest_span Storage for the instance to upgrade to
  virtual void
  upgrade(size_t src_array_size, std::span<const std::byte> src_span, std::span<std::byte> dest_span) const = 0;
};

/// Clockwork array upgrader that uses memcpy to upgrade the elements
class ClkMemcpyArrayUpgrader : public ClkArrayUpgrader
{
public:
  /// Constructor
  /// @param[in] min_array_size Minumum array size
  /// @param[in] max_array_size Maxumum array size
  /// @param[in] element_size Element size in bytes
  ClkMemcpyArrayUpgrader(size_t min_array_size, size_t max_array_size, size_t element_size);

  ~ClkMemcpyArrayUpgrader() noexcept override = default;

  ClkMemcpyArrayUpgrader(const ClkMemcpyArrayUpgrader&) = delete;
  ClkMemcpyArrayUpgrader& operator=(const ClkMemcpyArrayUpgrader&) = delete;
  ClkMemcpyArrayUpgrader(ClkMemcpyArrayUpgrader&&) = delete;
  ClkMemcpyArrayUpgrader& operator=(ClkMemcpyArrayUpgrader&&) = delete;

  /// @see ClkArrayUpgrader::upgrade
  void
  upgrade(size_t src_array_size, std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Minimum array size
  size_t min_array_size_;

  /// Maximum array size
  size_t max_array_size_;

  /// Element size in bytes
  size_t element_size_;
};

/// Clockwork array upgrader that uses an upgrader to upgrade the elements
class ClkTypeArrayUpgrader : public ClkArrayUpgrader
{
public:
  /// Constructor
  /// @param[in] min_array_size Minumum array size
  /// @param[in] max_array_size Maxumum array size
  /// @param[in] src_element_size Source element size in bytes
  /// @param[in] dest_element_size Destination element size in bytes
  /// @param[in] upgrader Element upgrader
  ClkTypeArrayUpgrader(
    size_t min_array_size,
    size_t max_array_size,
    size_t src_element_size,
    size_t dest_element_size,
    jewels::memory::ObjectPtr<const ClkTypeUpgrader> upgrader);

  ~ClkTypeArrayUpgrader() noexcept override = default;

  ClkTypeArrayUpgrader(const ClkTypeArrayUpgrader&) = delete;
  ClkTypeArrayUpgrader& operator=(const ClkTypeArrayUpgrader&) = delete;
  ClkTypeArrayUpgrader(ClkTypeArrayUpgrader&&) = delete;
  ClkTypeArrayUpgrader& operator=(ClkTypeArrayUpgrader&&) = delete;

  /// @see ClkArrayUpgrader::upgrade
  void
  upgrade(size_t src_array_size, std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Minimum array size
  size_t min_array_size_;

  /// Maximum array size
  size_t max_array_size_;

  /// Source element size in bytes
  size_t src_element_size_;

  /// Destination element size in bytes
  size_t dest_element_size_;

  /// Element upgrader
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> upgrader_;
};

/// Message region that compresses to a count of zeros in lite-compression
struct ClkZeroChunk
{
  /// Message offset
  size_t offset{};
  /// Zero region size in bytes
  size_t length{};

  auto operator<=>(const ClkZeroChunk&) const = default;
};

/// Schema aware lite-compressor interface
class ClkTypeLiteCompressor
{
public:
  /// Constructor
  ClkTypeLiteCompressor() noexcept = default;

  virtual ~ClkTypeLiteCompressor() = default;

  ClkTypeLiteCompressor(const ClkTypeLiteCompressor&) = delete;
  ClkTypeLiteCompressor& operator=(const ClkTypeLiteCompressor&) = delete;
  ClkTypeLiteCompressor(ClkTypeLiteCompressor&&) = delete;
  ClkTypeLiteCompressor& operator=(ClkTypeLiteCompressor&&) = delete;

  /// Locate the chunks of zeros that can be replaced with a count in the lite compressed message
  /// @param[in] data_span Data span containing the instance to compress
  /// @param[in] offset Instance message offset
  /// @param[in,out] zero_chunks Storage for the zero chunks
  /// @return Success of failure
  virtual jewels::BinaryOutcome compress(
    std::span<const std::byte> data_span,
    size_t offset,
    jewels::InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const;

  /// Compress opaque data by scanning for chunks of zeros
  /// @param[in] data_span Data span containing the instance to compress
  /// @param[in] offset Instance message offset
  /// @param[in,out] zero_chunks Storage for the zero chunks
  static void compress_opaque_data(
    std::span<const std::byte> data_span, size_t offset, jewels::InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks);
};

/// Clockwork array lite-compressor
class ClkArrayLiteCompressor
{
public:
  /// Constructor
  /// @param[in] max_size Maximum number of array elements
  /// @param[in] element_size Element size in bytes
  /// @param[in] compressor Element compressor
  ClkArrayLiteCompressor(size_t max_size, size_t element_size, std::shared_ptr<ClkTypeLiteCompressor> compressor);

  ~ClkArrayLiteCompressor() = default;

  ClkArrayLiteCompressor(const ClkArrayLiteCompressor&) = delete;
  ClkArrayLiteCompressor& operator=(const ClkArrayLiteCompressor&) = delete;
  ClkArrayLiteCompressor(ClkArrayLiteCompressor&&) = default;
  ClkArrayLiteCompressor& operator=(ClkArrayLiteCompressor&&) = default;

  /// Locate the chunks of zeros that can be replaced with a count in the lite compressed message
  /// @param[in] size Number of elements in the array
  /// @param[in] data_span Data span containing the instance to compress
  /// @param[in] offset Instance message offset
  /// @param[in,out] zero_chunks Storage for the zero chunks
  /// @return Success of failure
  jewels::BinaryOutcome compress(
    size_t size,
    std::span<const std::byte> data_span,
    size_t offset,
    jewels::InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const;

private:
  /// Maximum number of array elements
  size_t max_size_;

  /// Element size in bytes
  size_t element_size_;

  /// Array element compressor
  std::shared_ptr<ClkTypeLiteCompressor> compressor_;
};

/// Virtual clockwork type representation
class ClkType
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] metadata_version Schema metadata version
  ClkType(
    jewels::memory::MemoryResource memory_resource,
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    int32_t metadata_version);

  virtual ~ClkType() = default;

  ClkType(const ClkType&) = delete;
  ClkType& operator=(const ClkType&) = delete;
  ClkType(ClkType&&) = delete;
  ClkType& operator=(ClkType&&) = delete;

  /// @return Memory resource
  [[nodiscard]] const jewels::memory::MemoryResource& get_memory_resource() const noexcept;

  /// @return Fully qualified name
  [[nodiscard]] std::string_view get_fqn() const noexcept;

  /// @return Type ID
  [[nodiscard]] ClkTypeId get_type_id() const noexcept;

  /// @return Type index
  [[nodiscard]] size_t get_type_index() const noexcept;

  /// @return Schema metadata version
  [[nodiscard]] int32_t get_metadata_version() const noexcept;

  /// @return Type size in bytes
  [[nodiscard]] virtual size_t get_size() const noexcept = 0;

  /// @return Type alignment in bytes
  [[nodiscard]] virtual size_t get_alignment() const noexcept = 0;

  /// Make an initializer function to initialize this type to the specified value
  /// @param[in] value Initial enum value
  /// @return Initializer function
  [[nodiscard]] virtual std::shared_ptr<ClkValueInitializer>
  make_value_initializer(const metadata::InitialValue& value) const;

  /// Make an initializer function for this type (default implementation returns nullopt)
  /// @return Initializer function or nullopt if type does not require initialization
  [[nodiscard]] virtual std::optional<jewels::memory::ObjectPtr<const ClkValueInitializer>> make_initializer();

  /// Make a function to upgrade to this type from the source type
  /// @param[in] src_type Type to upgrade from
  /// @return Function to upgrade to this type from the source type
  [[nodiscard]] virtual jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) = 0;

  /// Check whether to use memcpy to upgrade to an array of this type from an array of the source type
  ///
  /// Using memcpy for upgrade is more efficient as it avoids having to upgrade elements one by one
  /// but memcpy can only be used when the source and destination are byte for byte compatible
  ///
  /// The default implementation returns false.
  /// @param[in] src_type Type to upgrade from
  /// @return True if the array can be upgraded to this type using memcpy
  [[nodiscard]] virtual bool use_memcpy_for_array_upgrade(const ClkType& src_type);

  /// Make a function to upgrade to an array of this type from an array of the source type
  /// @param[in] src_type Type to upgrade from
  /// @param[in] min_array_size Minimum array size
  /// @param[in] max_array_size Maximum array size
  /// @return Function to upgrade to this type from the source type
  [[nodiscard]] std::shared_ptr<ClkArrayUpgrader>
  make_array_upgrader(const ClkType& src_type, size_t min_array_size, size_t max_array_size);

  /// Legacy check for wire compatability for instances written before the built-in type UUID was added to the version
  /// @param[in] src_type Source type to check for commpatibility
  /// @return True if the source type is wire compatibile with this type
  [[nodiscard]] virtual bool is_legacy_wire_compatible(const ClkType& src_type) const = 0;

  /// Check for unexpected schema changes between this type and the source type
  /// @param[in] src_type Type to compare against
  /// @param[in] allow_changes Allow type changes
  /// @param[in] name Name to use in exception strings
  /// @throws runtime_error if an unexpected schema change is found
  virtual void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) = 0;

  /// Get the lowest underlying type
  /// @return Lowest underlying type
  [[nodiscard]] virtual ClkType& get_lowest_underlying_type();

  /// Test this type is the same type as the source type
  /// @param[in] src_type Type to compare against
  /// @return True if the source type is the same type as this type
  [[nodiscard]] virtual bool is_same_type(const ClkType& src_type) const;

  /// Make a lite-compressor instance to find the zero chunks in this type for lite-compression
  /// @return ClkLiteCompressor instance for this type
  [[nodiscard]] virtual jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> make_lite_compressor();

  /// @return True iff this type is compressible
  [[nodiscard]] virtual bool is_compressible();

protected:
  /// @return The map from source type index to upgrader for this type
  [[nodiscard]] std::pmr::unordered_map<size_t, jewels::memory::NonNullSharedPtr<const ClkTypeUpgrader>>&
  get_upgrader_cache();

  /// @return Reference to the cached lite compressor for this type
  [[nodiscard]] std::shared_ptr<ClkTypeLiteCompressor>& get_cached_lite_compressor();

  /// @return Reference to the cached flag indicating whether this type is compresssible
  [[nodiscard]] std::optional<bool>& get_cached_is_compressible();

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Fully qualified name
  std::pmr::string fqn_;

  /// Clockwork type ID
  ClkTypeId type_id_;

  /// Clockwork type index
  size_t type_index_;

  /// Schema metadata version
  int32_t metadata_version_;

  /// Cache of upgraders for this typee
  std::pmr::unordered_map<size_t, jewels::memory::NonNullSharedPtr<const ClkTypeUpgrader>> upgrader_cache_;

  /// Cached lite-compressor for this typee
  std::shared_ptr<ClkTypeLiteCompressor> cached_lite_compressor_;

  /// Cached flag indicating whether this type is compressible
  std::optional<bool> cached_is_compressible_;
};

/// Forward declaration
class ClkTypeFactory;

/// Clockwork type factory plugin interface
class ClkTypeFactoryPlugin
{
public:
  ClkTypeFactoryPlugin() noexcept = default;

  virtual ~ClkTypeFactoryPlugin() = default;

  ClkTypeFactoryPlugin(const ClkTypeFactoryPlugin&) = delete;
  ClkTypeFactoryPlugin& operator=(const ClkTypeFactoryPlugin&) = delete;
  ClkTypeFactoryPlugin(ClkTypeFactoryPlugin&&) = delete;
  ClkTypeFactoryPlugin& operator=(ClkTypeFactoryPlugin&&) = delete;

  /// Construct a clockwork type from a schema type protobuf if the type is handled by the plugin
  /// @param[in] factory Clockwork type factory used to make underlying types
  /// @param[in] type_proto Clockwork type protobuf
  /// @param[in] type_index Type index
  /// @param[in] maybe_strong_type_fqn Optional fully qualified name of a strong type wrapping this schema
  /// @return Clockwork type instance or nullptr if the schema type was not handled
  /// @throws runtime_error on failire
  [[nodiscard]] virtual std::shared_ptr<ClkType> make_clk_type(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    const metadata::TypeDesc& type_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn) const = 0;
};

/// Clockwork type factory
class ClkTypeFactory
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] metadata_proto Tachyon metadata protobuf
  /// @param[in] plugins Factory plugins used to create clockwork types
  ClkTypeFactory(
    jewels::memory::MemoryResource memory_resource,
    std::shared_ptr<const metadata::TachyonMetadata> metadata_proto,
    std::pmr::vector<std::shared_ptr<ClkTypeFactoryPlugin>> plugins);

  ClkTypeFactory();

  ~ClkTypeFactory() = default;

  ClkTypeFactory(const ClkTypeFactory&) = delete;
  ClkTypeFactory& operator=(const ClkTypeFactory&) = delete;
  ClkTypeFactory(ClkTypeFactory&&) = delete;
  ClkTypeFactory& operator=(ClkTypeFactory&&) = delete;

  /// Get the clockwork type for the type protobuf at the specified type index
  /// @param[in] type_index Type index
  /// @throws std::runtime_error on failure
  [[nodiscard]] jewels::memory::ObjectPtr<ClkType> get_clk_type(size_t type_index);

  /// @return Tachyon metadata protobuf
  [[nodiscard]] const metadata::TachyonMetadata& get_metadata_proto() const noexcept;

  /// @return Tachyon metadata version
  [[nodiscard]] int32_t get_metadata_version() const noexcept;

  /// @return Representations for the types in the protobuf schema
  [[nodiscard]] std::pmr::vector<std::shared_ptr<ClkType>>& get_types() noexcept;

  /// @return Memory resource used for allocations
  [[nodiscard]] const jewels::memory::MemoryResource& get_memory_resource() const noexcept;

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Tachyon metadata protobuf
  std::shared_ptr<const metadata::TachyonMetadata> metadata_proto_;

  /// Factory plugins used to create clockwork types
  std::pmr::vector<std::shared_ptr<ClkTypeFactoryPlugin>> plugins_;

  /// Representations for the types in the protobuf schema
  std::pmr::vector<std::shared_ptr<ClkType>> types_;
};

/// Check for unexpected history changes
/// @param[in] memory_resource Memory resource
/// @param[in] src_became Map of field renumbering in source schema
/// @param[in] src_removed Fields removed from the source schema
/// @param[in] dst_became Map of field renumbering in destination schema
/// @param[in] dst_removed Fields removed from the destination schema
/// @param[in] allow_changes True to allow changes in the schema history
/// @param[in] name Name used in error messages
/// @throws runtime_error if unexpected changes are found
void check_for_unexpected_history_changes(
  const jewels::memory::MemoryResource& memory_resource,
  const std::pmr::map<int32_t, int32_t>& src_became,
  const std::pmr::set<int32_t>& src_removed,
  const std::pmr::map<int32_t, int32_t>& dst_became,
  const std::pmr::set<int32_t>& dst_removed,
  bool allow_changes,
  std::string_view name);

} // namespace clockwork::serialization
