# Schemas, representations, and interfaces

Clockwork makes a distinction that most schema languages do not: schemas as a concept are distinct from representations of the schemas and interfaces to those representations.

## Schemas

The _schema_ is a logical data model, specifying fields and abstract data types.
These data types are independent of any programming language, serialization wire format, or in-memory representation.
This allows Clockwork schemas to support multiple different serialization backends and in-memory representations.

So because a schema is an abstract data model, it cannot ever be directly instantiated.
You can't construct an instance of a schema in C++ or any other language because a schema has no representation.

## Representations

A _representation_ is a concrete realization of an abstract schema, in bits and bytes.
A representation can exist in memory, on a network link, or on disk.
Some representations only support in-memory usage, but most are suitable for serialization, meaning they can be transmitted over a network or written to and read from storage.

Representations currently supported by the Clockwork MVP: Tachyon and Protobuf.

### Tachyon

_Tachyon_ is Clockwork's native, highly efficient binary representation, optimized for on-vehicle use.
It supports direct access, where data is written and read directly in Tachyon format in memory, which can then just be directly transmitted over a network or written to disk.
No separate serialize/deserialize step is required.
On-disk data can just be memory mapped and accessed directly for both reading and writing.

The downside of Tachyon is that all data is always sized for the worst-case.
When there are variable-sized containers, every container must have a maximum size, and Tachyon always requires that much memory or storage to be allocated in advance.

### Protobuf

_Protobuf_ is a standard open serialization format.
It includes its own schema language, so when using Protobuf representations, Clockwork first generates a Protobuf schema from the Clockwork schema.
Then any Protobuf-supported language can be used to access the data.
Both binary and textproto formats are supported.

### Looking ahead

Representations we expect to support in the future include SOME/IP (for communicating with embedded systems) and gRPC (for communicating with cloud services—this of course builds on Protobuf support).

We also expect to have a Native representation which is in-memory only, is not portable, and requires separate serialization/deserialization to some other representation in order to be transmitted or stored.

## Interfaces

_Interfaces_ are the programming interfaces used to access schema data in some representation.
They are always language-specific and take the form of an actual API in that language.

It's possible to have a common interface for multiple representations of the same schema.
So for example if we have both Tachyon and Protobuf representations of a schema, you could have a common API for accessing that representation, instead of two different APIs.
This can make code substantially more flexible.

### Tap

The primary C++ interface type in Clockwork is called _Tap_.
Tap is designed to tap into the maximum performance offered by Tachyon representations, while still providing a clean abstraction layer over the concrete representation.
Tap interfaces, despite abstracting the underlying representation, are zero-cost abstractions in most cases, or occasionally add very low cost.

Tap can also be generated for other representations, such as Protobuf, so that C++ code that needs to deal with both representations can do so in a consistent way, even sharing code directly that can deal with both representations without any changes.

Tap generally provides access to data via getter and setter methods.
When dealing with containers, instead of directly exposing the underlying container, Tap provides views to the data, such as spans, ranges, and iterators.
Existing code which hard-codes container types such as `std::vector` or `std::pmr::vector` cannot be used with Tap directly; it needs to be modified to be based on these view types or on Tap's own container types.
Note that after changing code to use ranges, spans, and iterators, that code can then be used with either Tap data or native containers like `std::vector`, which also can provide the same view types, so it does not make the code Tap-specific.

### Dataclasses

In Python, the primary interface to Clockwork data is normal Python dataclasses.
However, this comes at a performance cost, as essentially we must serialize and deserialize between these dataclasses and some other representation, such as Tachyon or Protobuf.
For large messages this can be a heavy cost, so when dealing with large messages even offboard, C++ (and Tap/Tachyon) is preferred.

### Protobuf

When using Protobuf representations, the native Protobuf API can also be used, bypassing Clockwork's own interface mechanism.
This of course makes the code Protobuf-specific.
We cannot create a Protobuf-style interface to Tachyon data for example.
