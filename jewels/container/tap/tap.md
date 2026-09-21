# Tap

Tap is a library of C++ containers that implement the Tachyon layout for Clockwork serialization.
Details on the Tachyon layout can be found in the [Tachyon Readme](/platforms/clockwork/clockwork/serialization/tachyon.md).

## Bitset

`Bitset<N>` is a fixed-size, byte-oriented collection of `N` bits for Tachyon fields.
It has exactly `ceil(N / 8)` bytes of storage and one-byte alignment.
Bit `i` uses mask `1 << (i % 8)` in byte `i / 8`, so its representation does not depend on native integer endianness.

Use `test`, `try_set`, `set`, `try_reset`, `reset`, `try_flip`, `flip`, `all`, `any`, `none`, and `count` for logical access.
`test(jewels::Out<bool>, index)`, `try_set`, `try_reset`, and `try_flip` are callsig-safe for an index that may be invalid.
The throwing `test`, `set`, `reset(index)`, and `flip(index)` overloads are only for STL algorithm compatibility.
`bytes()` exposes only a read-only byte span.

## VarArray

`VarArray` is a fixed capacity container that conforms to the Tachyon layout.
Use cases for `VarArray` are similar to `boost::static_vector`.
However, `VarArray` enforces some special properties such as guaranteeing that padding between fields is zero'd out and it requires the value type to be trivially destructible.
`VarArray` is used to represent contiguous containers in the [Clockwork Schema](/platforms/clockwork/clockwork/docs/reference/schemas.md) language.
Due to its special properties, this container might not be best suited for general algorithm development.

## Callsig APIs

Tap preserves selected throwing APIs for generic STL-compatible code.
Code that names a Tap container directly should use the corresponding callsig overload, which returns `jewels::BinaryOutcome` and writes produced values through an output parameter.
Legacy `try_set`, `try_pop_back`, and `try_emplace_back` return types are retained as deprecated migration shims; pass `jewels::tap::callsig` to select the outcome-returning `try_set` and `try_pop_back` overloads.
