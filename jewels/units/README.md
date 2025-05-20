# Physical units library

A library for physical units based on [Au](https://aurora-opensource.github.io/au/0.3.2/).

## Unit tests

To render values for failed checks in unit tests, include `io.hh`:

```cpp
#include "jewels/units/io.hh"
```

## Examples

### Distance

```cpp
#include "jewels/units/meters.hh"
#include "jewels/units/miles.hh"

constexpr auto some_value = au::meters(1000.0); // You can also use UDLs, for example `1000_meters`
some_value.in(meters); // -> 1000.0
some_value.in(miles); // -> ~0.621;
```

### Time

```cpp
#include "jewels/units/minutes.hh"
#include "jewels/units/seconds.hh"

constexpr auto some_value = au::minutes(1); // You can also use UDLs, for example `1_minutes`
some_value.in(minutes); // -> 1
some_value.in(seconds); // -> 60
```
