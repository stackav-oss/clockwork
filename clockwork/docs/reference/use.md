# Importing other Clockwork modules with "use"

## Examples

Clockwork allows modules to import definitions from other modules using "use" syntax.
Let's look at some examples.

### A simple module import

```clockwork
use clockwork::examples::helloworld::schema;
```

This will attempt to load the file `"clockwork/examples/helloworld/schema.clk"`, relative to the workspace root.
Everything defined in `schema.clk` will be accessible by name under the namespace `schema::`.
So if it defined a schema `SimpleSchema`, that would be accessible as `schema::SimpleSchema`.

This is generally the recommended way of importing other modules, because it leaves the names from that module namespaced within that module.
But sometimes it's more desirable to import specific names from that module into the local namespace.

### Importing a specific definition

```clockwork
use clockwork::examples::helloworld::schema::SimpleSchema;
```

This imports `SimpleSchema` as a name directly into the local namespace.
The general rule is that whatever is the final component of the `use` path is the name that appears in the local namespace.

> [!NOTE]
> Names imported from other modules, through whatever mechanism, are not re-exported when this module is imported by another!
> This is different from some other languages, and follows an "include what you use" philosophy.
> You never get names transitively included from other modules; you must always include names you use yourself.

### Importing multiple things from the same module

```clockwork
use clockwork::examples::helloworld::schema::{SimpleSchema, OtherSchema};
```

We also have shortcuts for importing multiple names at once; here we import both `SimpleSchema` and `OtherSchema` from the `schema.clk` module at the same time.
Both are under the `schema::` namespace.
We do not support wildcard imports; you can either import the entire module under a namespace, or enumerate the specific names you want.

Multiple-import form works with modules themselves also.

### Importing multiple modules from the same path

```clockwork
use clockwork::examples::helloworld::{schema, cogs};
```

This imports `clockwork/examples/helloworld/schema.clk` as namespace `schema::` and `clockwork/examples/helloworld/cogs.clk` as namespace `cogs::`.

### Aliases

```clockwork
use clockwork::examples::helloworld::schema::SimpleSchema as HelloWorldSchema;
```

This shows how you can change a name as you import it.
It works with modules and multiple-import as well.

### More aliases

```clockwork
use clockwork::examples::helloworld::schema::{SimpleSchema as HelloWorldSchema, OtherSchema as OtherHelloWorldSchema};
use clockwork::examples::helloworld::schema as helloworld_schema;
use clockwork::examples::helloworld::{schema as helloworld_schema, cogs as helloworld_cogs};
```

When things get complicated like this, it's useful to reformat this in block format.

### Block format

```clockwork
use clockwork::examples::helloworld::schema::
{
    SimpleSchema as HelloWorldSchema;
    OtherSchema as OtherHelloWorldSchema;
}
```

You can also nest this syntax in various ways.

### Complex use blocks

```clockwork
use clockwork::examples::helloworld::
{
    schema::
    {
        SimpleSchema as HelloWorldSchema;
        OtherSchema as OtherHelloWorldSchema;
    };
    cogs::
    {
        ConsumerCog as HelloWorldConsumerCog;
        ProducerCog as HelloWorldProducerCog;
    }
}
```

But please note, just because you _can_ construct arbitrarily deep nesting does not mean you should.
Using this sparingly increasing readability, but overdoing it just makes things confusing.

### External repositories

Clockwork allows you to import Clockwork modules from external dependencies as well.

A use statement specified as `use a::b::c` is assumed by the compiler to be within the same repository.

Cross repositories would provide a prefix of `@<repo name>` to the identifier.

For example, if module `use a::b::c` is from an external repository `ext_repo`, the use statement would look like `use @ext_repo::a::b::c`.

The repository prefix is part of all fully qualified symbol names and is used by the compiler to augment search paths for Clockwork module files.

Even if a repository prefix is not specified, internally the compiler adds the prefix for the current repository to any symbol name.
