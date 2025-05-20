# Tap

Tap is a library of C++ containers that implement the Tachyon layout for Clockwork serialization.
Details on the Tachyon layout can be found in the [Tachyon Readme](/platforms/clockwork/clockwork/serialization/tachyon.md).

## VarArray

`VarArray` is a fixed capacity container that conforms to the Tachyon layout.
Use cases for `VarArray` are similar to `boost::static_vector`.
However, `VarArray` enforces some special properties such as guaranteeing that padding between fields is zero'd out and it requires the value type to be trivially destructible.
`VarArray` is used to represent contiguous containers in the [Clockwork Schema](/platforms/clockwork/clockwork/docs/reference/schemas.md) language.
Due to its special properties, this container might not be best suited for general algorithm development.
