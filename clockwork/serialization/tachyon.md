# Tachyon serialization

## Introduction

Tachyon is the primary on-vehicle serialization format, making it also the primary logged message format.
It is designed for highly efficient access from C++ code on both CPUs and GPUs.
It allows efficient direct access to in-memory data in Tachyon format directly, eliminating the need for separate serialization and deserialization steps.
But it also has a platform-independent, language-independent, well-defined binary layout for every schema, making it suitable for transmission, storage, and long-term access with backward compatibility.
(This is in contrast to using, for example, native plain C++ structs, which would also allow efficient direct access, but do not have a cross-platform, cross-language, long-term stable binary layout.)

The price of allowing this kind of direct access without overhead is that all Tachyon data must be _fixed-size_ in memory, even when there are variably-sized containers present in the schemas.
This allows the entire schema layout to be fixed in size with every field at a fixed, statically-known offset from the beginning of the schema.
This in turn allows zero-overhead, native-speed access to the data in already-serialized form.

To achieve this, even variable-sized containers must have a maximum size, and Tachyon will always consume worst-case memory, even when containers have fewer elements than the worst case.
This is unacceptable in many applications but for our application in Clockwork, it works well with the general Clockwork philosophy of bounded resource utilization with pre-allocation for the worst case.
In this context specifically, it is exactly what we want anyway.

This approach allows Tachyon data to be stored in shared memory with no extra cost, and transmitted over a network, PCIe bus, or stored to disk with `memcopy` or DMA transfers.
When data is stored on disk, in fact, the file can simply be memory-mapped and directly accessed, for both reading and writing.[^1]

[^1]: We cannot use the memory-mapping trick with our own vehicle logs, which are not simply Tachyon data but rather Tachyon data inside a more dynamic container format, but we can use it for other situations, such as config files, stored state snapshots, and world maps.

### Endianness

Tachyon data is always stored little-endian, regardless of hardware platform.
This is the most common bit order in use today, and is what's used on our target hardware.

## Layout algorithm

As mentioned above, every Clockwork schema has a statically-known, well-defined, platform- and language-independent layout, with every byte and bit location being defined by the layout.
Here we define the algorithm that determines that layout, given a Clockwork schema.

> [!NOTE]
> Clockwork allows generic schemas to be defined, which have generic parameters, like C++ templates.
> We cannot, however, generate a generic layout, because fundamentally the layout must determine every byte offset for every field, so every field must have known size and alignment.
> So we must fully instantiate the Clockwork schema prior to generating the layout.

### Field ordering

The algorithm first needs a deterministic total ordering of fields.
This is _not_ necessarily the order that the fields will end up in, but it's the order in which the algorithm processes the fields when computing the layout.
Very often the fields will end up in a different order in the layout as a result of the design of the layout algorithm.

Fields in Clockwork schemas are ordered by field numbers.
Field numbers must not repeat, so they provide a total ordering of fields.
We use this property as part of ensuring that we have a deterministic field ordering for the algorithm.

We also use the size and alignment of each field as part of the ordering.
Specifically, fields are ordered by the tuple:

```python
(max(size, align), min(size, align), field_num)
```

We order in this way so that we process the most constrained fields first.
A field is constrained by being big or by being aligned to a large value, or both.
A field with large size _and_ large alignment is more constrained than one with large alignment but small size, so this ordering gives us what we want.
(We'll see shortly why this is what we want.)
The field number is added as the final (least significant) element to guarantee that there is a deterministic total ordering even when sizes and alignments of some fields are equal.

### Layout algorithm

Fundamentally, this problem is a variation of the 1-D [Cutting Stock Problem](https://en.wikipedia.org/wiki/Cutting_stock_problem), also related to the [Knapsack Problem](https://en.wikipedia.org/wiki/Knapsack_problem).
These are NP-hard problems, and the best general optimization approach would probably be integer linear programming.

In lieu of that complexity, we choose a very straightforward $O(Nlog(N))$ greedy algorithm which, we believe, will generate near-optimal results for most real-world examples.
We could always introduce Tachyon2 later based on ILP optimization, if we find real-world examples where the greedy algorithm is significantly wasteful.

The idea of the algorithm is to iterate over fields, most constrained first, and find a home for them in the layout.
If there is an existing gap in the layout that meets the field's constraints, then the field will be placed in that gap.
If there is no gap that accommodates the field, then free bytes are added to the end of the layout as necessary to accommodate it.
The field is always placed as close to the beginning of the free space it's being fit into as its alignment allows.

Later fields can be fit into gaps between earlier fields.
This is by design, in order to more efficiently use space while honoring alignment requirements.
There is no benefit to fields being "in order" in the layout, but there is substantial benefit to making the layout compact.[^2]

[^2]:
    This is not _completely_ true in all cases.
    For very large schemas, it's possible that cache locality could be improved by considering it in the layout, but this is a rare niche case, and we would then need a way for developers to specify locality preferences.
    This complexity does not seem worth it.
    Generally speaking, a compact, contiguous layout is already very cache-friendly.
    We process fields in this order specifically to maximize the chances of being able to slot less constrained fields in between more constrained fields.
    The out-of-order layout is a feature rather than a bug.

Note that we must not only sort the inputs but also keep track of gaps during this algorithm.
Gaps can be stored as sorted intervals, smallest and least-aligned first.
Finding a suitable gap will be a reasonably fast operation, and the number of gaps is at most $N-1$ where $N$ is the number of fields.

### Laying out bounded, variable-sized containers

Clockwork `VarArray` and `VarString` are implemented as fixed-size storage followed by 8-byte size.
We do not consider the size and the storage as separate fields during layout, which may lead to some inefficiencies in packing.
Frequently, the size field could be tucked into a gap left elsewhere in the layout.
But trying to take advantage of this would mean that we would have to codegen a customized container class for every layout.
That would require any user code that needs to reference the container type directly to be templated, because each version of each schema would have a different container type.

So we accept the small inefficiency caused by having to store the size and data together, and we treat them as one field in the layout algorithm with appropriate size and alignment.
There may even be an internal gap in this data-and-size field, depending on size and alignment of the data.

Note that we store first the data and _then_ the size because the data may need to be precisely aligned, but the size will not need precise alignment, and since we'll be treating these things as an inseparable unit, we really can only align the first field.

## Generating layout classes

The result of this algorithm is a byte span (offset and size) for every field, relative to the beginning of the schema.
This feeds into the next stage, which is generating classes which implement this layout in our target languages C++ and Python.

### C++ layout structs

In C++, we generate implicit-lifetime structs with `__attribute__((packed))` so that we have complete control over the layout.
So we must generate padding fields ourselves, as `char[N]` arrays.

All fields are generated as public, because these structs are not intended to be used directly by users.
They are wrapped in Tap interfaces for user code.

We also generate unit tests (mostly `static_assert`s) for every layout that check that the compiler honors the `packed` attribute as we expect it to, and that every field ends up with the expected offset and size.

### Python layout classes

In Python, we use the built-in [struct](https://docs.python.org/3/library/struct.html) library to generate the layout.
We do not try to take advantage of direct access to serialized data as we do in C++; instead we implement serialize and deserialize functions, using the `struct` library to do the heavy lifting.
We marshall between the serialized form and Python dataclasses, which then also serve as the user interface to the data.
The dataclasses themselves are generated, so that they can be type-checked with Pyrefly, but it's also possible for this to be used in an entirely dynamic workflow where the generated classes are never written to disk but compiled and instantiated entirely in memory at runtime.
This lets us read the schemas from a log and dynamically create Python dataclasses and marshalling code for that exact schema version.
