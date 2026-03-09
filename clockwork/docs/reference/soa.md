# Struct-of-Arrays (SoA) Containers

This document describes the Struct-of-Arrays container types `FixedSoa` and `VarSoa`, which provide an alternative to Array-of-Structs (`FixedArray` and `VarArray`) data layouts.

## Overview

Struct-of-Arrays (SoA) is a data layout pattern where each field of a struct is stored in a separate contiguous array, rather than interleaving all fields for each element (Array-of-Structs/AoS).

**Array-of-Structs (AoS) layout:**

```text
[{x0, y0, z0}, {x1, y1, z1}, {x2, y2, z2}, ...]
```

**Struct-of-Arrays (SoA) layout:**

```text
x: [x0, x1, x2, ...]
y: [y0, y1, y2, ...]
z: [z0, z1, z2, ...]
```

Clockwork provides `FixedSoa<T, size>` and `VarSoa<T, max_size>` container types, analogous to `FixedArray<T, size>` and `VarArray<T, max_size>`, but using SoA data layout.

## Benefits of SoA

### Reduced Memory Padding

SoA layout can be more memory-efficient than AoS because it avoids padding between elements.
For example, a struct with fields of different sizes (e.g., `Float32`, `Float32`, `Float32`, `UInt8`) would require padding in AoS form to maintain alignment, but each field array in SoA form is naturally aligned without internal padding.

### Improved Cache Locality

When processing only a subset of fields across many elements, SoA layout provides better cache locality because the data for each field is stored contiguously.
This can improve performance for certain algorithms that iterate over single fields.

### Vectorization Opportunities

Contiguous storage of field data enables SIMD (Single Instruction, Multiple Data) vectorization for operations that process one field at a time across many elements.

## When to Use SoA

Consider using SoA when:

- Your data has padding overhead in AoS form
- Algorithms frequently access only a subset of fields
- Vectorization of field operations is beneficial
- Message size reduction is important

Use AoS (`FixedArray`/`VarArray`) when:

- All fields are accessed together for each element
- Element-wise operations dominate
- Simpler API is preferred

## Schema Syntax

### Enabling SoA Code Generation

To use SoA containers with a schema type, add `soa_enabled: true` to the schema's options:

```clockwork
// 3D point
schema Point3f
{
  uuid: 11111111-1111-1111-1111-111111111111;
  options
  {
    soa_enabled: true;
  }
  fields
  {
    // X coordinate
    #0 x: Float32;
    // Y coordinate
    #1 y: Float32;
    // Z coordinate
    #2 z: Float32;
  }
}
```

This does **not** cause all instances of this schema to be SoA layout!
This simply makes that schema eligible to be used as the element type of `FixedSoa` and `VarSoa` containers.
When used outside of those containers it's still just a normal schema.

You can add or remove this option without changing the schema version number.

### Declaring SoA Fields

Once a schema has `soa_enabled: true`, you can use it with `FixedSoa` or `VarSoa`:

```clockwork
// Container using fixed-size SoA
schema PointCloud
{
  uuid: 22222222-2222-2222-2222-222222222222;
  fields
  {
    // Fixed-size SoA of 100 points
    #0 points: FixedSoa<Point3f, size=100>;
  }
}

// Container using variable-size SoA
schema DynamicPointCloud
{
  uuid: 33333333-3333-3333-3333-333333333333;
  fields
  {
    // Variable-size SoA with max 1000 points
    #0 points: VarSoa<Point3f, max_size=1000>;
  }
}
```

The syntax is analogous to `FixedArray<T, size>` and `VarArray<T, max_size>`:

| Container Type          | Description                            |
| ----------------------- | -------------------------------------- |
| `FixedSoa<T, size=N>`   | Fixed-size SoA with exactly N elements |
| `VarSoa<T, max_size=N>` | Variable-size SoA with 0 to N elements |

### Supported Field Types in SoA Elements

SoA element schemas can contain any field types that are supported in normal schemas, including containers and nested sub-schemas.
However, these complex structures are not recursively "flattened" SoA-style; a nested sub-schema will be stored as AoS, but within a single array of the top-level SoA schema.
Similarly, a schema containing a VarArray will result in an array of VarArrays to hold that field.

For example, consider a schema with a nested sub-schema:

```clockwork
schema Particle
{
  options { soa_enabled: true; }
  fields
  {
    #0 id: UInt32;
    #1 position: Point3f;  // nested sub-schema
  }
}
```

A `VarSoa<Particle, 3>` would have this layout:

```text
┌────────────────────────────────────────────────┐
│ VarSoa<Particle, 3>                            │
├────────────────────────────────────────────────┤
│ id:       [id0, id1, id2]                      │  <- Contiguous UInt32s
│ position: [{x0,y0,z0}, {x1,y1,z1}, {x2,y2,z2}] │  <- Array of Point3f structs
│ size:     3                                    │
└────────────────────────────────────────────────┘
```

The `id` field is stored as a contiguous array of `UInt32` values (SoA-style).
The `position` field is stored as a contiguous array of `Point3f` structs (AoS within SoA).

## C++ API

The generated C++ API provides both element-oriented access (via proxy references) and field-array access (via spans).

### Container Types

The code generator produces specializations of the `FixedSoa` and `VarSoa` template types:

```cpp
#include "my_schema.hh"

clockwork::FixedSoa<Point3f, 100> fixed_soa;  // Fixed-size, always 100 elements
clockwork::VarSoa<Point3f, 100> var_soa;      // Variable-size, 0 to 100 elements
```

### Container Methods

Both `FixedSoa` and `VarSoa` provide similar container interfaces:

| Method            | Return Type     | Description                                |
| ----------------- | --------------- | ------------------------------------------ |
| `operator[](i)`   | `ElementRef`    | Access element at index (unchecked)        |
| `at(out, i)`      | `BinaryOutcome` | Access element at index (checked, callsig) |
| `at(i)`           | `ElementRef`    | Access element at index (throws on error)  |
| `size()`          | `size_t`        | Current number of elements                 |
| `capacity()`      | `size_t`        | Maximum capacity                           |
| `empty()`         | `bool`          | Whether container has no elements          |
| `begin()`/`end()` | Iterator        | Range iteration                            |
| `view_<field>()`  | `std::span<T>`  | Direct access to field array               |

**VarSoa-only methods:**

| Method                  | Return Type     | Description                                        |
| ----------------------- | --------------- | -------------------------------------------------- |
| `resize(n)`             | `void`          | Resize to n elements (throws on overflow)          |
| `try_resize(n)`         | `BinaryOutcome` | Resize to n elements (returns failure on overflow) |
| `clear()`               | `void`          | Remove all elements                                |
| `emplace_back(...)`     | `ElementRef`    | Add element at end (throws on overflow)            |
| `try_emplace_back(...)` | `BinaryOutcome` | Add element at end (returns failure on overflow)   |

### Element Proxy References

Accessing individual elements returns a proxy reference (`ElementRef` or `ElementConstRef`) that provides field accessors:

```cpp
clockwork::VarSoa<Point3f, 100> soa;
soa.resize(10);

auto elem = soa[0];  // Returns ElementRef

// Field accessors
elem.get_x();            // const float&
elem.get_mutable_x();    // float&
elem.set_x(1.0f);        // void

// Assignment from Tap/TapInit
elem = clockwork::TapInit<clockwork::Tachyon<Point3f>>{.x = 1.0f, .y = 2.0f, .z = 3.0f};
```

The proxy reference API matches the standard Tap interface for the element schema.

### Direct Field Array Access

For bulk operations, access the underlying field arrays directly via `view_<field>()` methods:

```cpp
clockwork::VarSoa<Point3f, 100> soa;
soa.resize(50);

// Direct field access for vectorized operations
auto x_span = soa.view_x();  // std::span<float>
auto y_span = soa.view_y();  // std::span<float>
auto z_span = soa.view_z();  // std::span<float>

// Bulk operation on x values
for (auto& x : x_span)
{
  x *= 2.0f;
}
```

### STL Compatibility

SoA containers work with STL algorithms:

```cpp
clockwork::VarSoa<Point3f, 100> soa;
soa.resize(10);

// Range-based iteration
for (auto elem : soa)
{
  elem.set_x(0.0f);
}

// STL algorithms work via iterators
std::iter_swap(soa.begin(), soa.begin() + 1);

// Element swap via proxy references
auto ref0 = soa[0];
auto ref1 = soa[1];
swap(ref0, ref1);  // ADL-found swap
```

### Example: Complete Usage

```cpp
#include "point3f.hh"  // Generated from schema with soa_enabled: true

void process_points()
{
  clockwork::VarSoa<Point3f, 1000> soa;

  // Add elements via emplace_back
  for (int i = 0; i < 100; ++i)
  {
    auto ref = soa.emplace_back();
    ref.set_x(static_cast<float>(i));
    ref.set_y(static_cast<float>(i * 2));
    ref.set_z(static_cast<float>(i * 3));
  }

  // Or use TapInit
  soa.emplace_back(clockwork::TapInit<clockwork::Tachyon<Point3f>>{
    .x = 100.0f, .y = 200.0f, .z = 300.0f
  });

  // Access individual elements
  auto elem = soa[50];
  float x = elem.get_x();

  // Bulk field access
  auto z_values = soa.view_z();
  for (auto& z : z_values)
  {
    z += 10.0f;
  }

  // Convert element to Tap
  auto tap = static_cast<clockwork::Tap<clockwork::Tachyon<Point3f>>>(soa[0]);
}
```

## Python API

The Python API for SoA types uses dynamically-generated dataclasses with field arrays represented as lists.

### Basic Structure

SoA containers in Python are represented as dataclasses where each field is a list:

```python
# SoA types are generated as dataclasses
# VarSoa fields have default empty lists
# FixedSoa fields require all values at construction

# For VarSoa<Point3f, max_size=10>:
var_soa = VarSoa_Point3f_10(x=[1.0, 2.0], y=[3.0, 4.0], z=[5.0, 6.0])

# For FixedSoa<Point3f, size=3>:
fixed_soa = FixedSoa_Point3f_3(x=[1.0, 2.0, 3.0], y=[4.0, 5.0, 6.0], z=[7.0, 8.0, 9.0])
```

### Accessing Field Data

Access field arrays as attributes:

```python
msg = deserialize_message(buffer)

# Access field arrays
x_values = msg.points.x  # list[float]
y_values = msg.points.y  # list[float]
z_values = msg.points.z  # list[float]

# Modify field values
msg.points.x[0] = 10.0
```

## Schema Evolution

SoA containers support Clockwork's schema evolution features.

### Converting Between AoS and SoA

Fields can be converted between Array-of-Structs and Struct-of-Arrays layouts as a standard field type change:

```clockwork
// Version 1: AoS
schema Container
{
  fields
  {
    #0 points: VarArray<Point3f, max_size=100>;
  }
}

// Version 2: SoA
schema Container
{
  fields
  {
    #0 points: VarSoa<Point3f, max_size=100>;
  }
  history
  {
    version: 2;
  }
}
```

The upgrade system automatically transposes data between layouts.

### Changing Element Schema

When the element schema evolves, SoA containers upgrade each element accordingly.
Field additions, removals, and type changes within the element schema are handled transparently.

### The soa_enabled Option

The `soa_enabled` schema option can be added or removed freely without affecting schema compatibility:

- It only controls C++ code generation for SoA APIs
- It does not affect the schema version or layout
- Pre-existing data remains readable
- If removed while `VarSoa`/`FixedSoa` references exist, those will fail to compile

## Best Practices

1. **Use SoA for large collections with padding overhead**: SoA provides the most benefit when the element type has significant padding in AoS form.

2. **Use SoA when accessing subsets of fields**: If your algorithms typically process only some fields, SoA's cache locality helps.

3. **Prefer field array access for bulk operations**: Use `view_<field>()` methods instead of iterating with element proxies when operating on entire field arrays.

4. **Keep element access for complex operations**: When accessing multiple fields per element, the proxy reference API keeps code readable.

5. **Profile to verify benefits**: The performance difference between AoS and SoA depends on access patterns and hardware.
   Measure to confirm SoA provides actual benefit for your use case.
