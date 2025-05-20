# Clockwork schema evolution guidance

First, see the [schema evolution reference](../reference/schema_history.md) for a detailed explanation of schema evolution mechanics.
This document discusses other system-level concerns with changing schemas.


## Changing to a different schema

Schema evolution means changing the definition of one schema.
But you might also want to change which schema is used on a given channel.
This we have no support for currently.

**This includes changing the generic parameters used to instantiate generic schemas.**
A different instantiation of the same schema is a different schema.
We can lift this restriction in the future, but the MVP for schema evolution does not support this.
