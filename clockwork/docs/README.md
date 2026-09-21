# Clockwork

In this repo, you'll find all the user-facing documentation for the Clockwork infrastructure framework.

## First steps

Start with these pages to learn the fundamentals of Clockwork:

- Read [Clockwork 101](clockwork101.md) for a high-level overview of the system.
- Familiarize yourself with Clockwork's key terms found in the [Glossary](glossary.md).
- Create a simple Clockwork system following the instructions found in the [Hello, World! tutorial](tutorial/helloworld.md).

## Further reading

Dig into the Clockwork schema language starting with these pages:

- [Common syntax reference](reference/common_syntax.md): guide for common syntax elements in the Clockwork language.
- [Schema language complete reference guide](reference/schemas.md): details the Schema-related features of the Clockwork language.
- [Schema representations and interfaces reference guide](reference/repr_iface.md): guide for defining and customizing schema representations and interfaces.
- [Schemas, representations, and interfaces concept guide](concepts/schema_repr_iface.md): introduction to the concepts of schemas, representations, and interfaces.
- [Struct-of-Arrays containers](reference/soa.md): reference for `FixedSoa` and `VarSoa` container types which provide SoA data layout.
- [Attributes and code generation](reference/attributes.md): reference for the attributes that tell the compiler what code to generate.
- [Parameterized channels, cogs and boxes](reference/parameterized.md): reference for using parameters to define channels, cogs and boxes.

For system composition and Cog execution:

- [System composition guide](concepts/composition.md): explains how to compose Cogs, state, and config into systems, including [init Cogs and state initialization](concepts/composition.md#init-cogs-and-state-initialization).
- [Multi-connect inputs](concepts/multi_connect_inputs.md): explains how arrays of channels can be connected to a cog input..
- [Multi-message outputs](concepts/multi_message_outputs.md): explains how Cogs can publish a bounded number of messages from one output endpoint in a single execution.
- [Execution conditions reference](reference/exec_conditions.md): details execution conditions including the `init` condition for init Cogs.

For runtime expressions and computations:

- [DFL (Declarative Functional Language)](reference/dfl/README.md): a pure-functional sublanguage for runtime computations such as signal transforms, detector conditions, and aligner constraints.

## Guidance

For guidance on best practices for using Clockwork features, read:

- [Init value guidance](guidance/init_values.md): explains how and when to use initialization specifications in Clockwork schemas.
  This is not just a description of the init value mechanism, but also a guide on how to think about data initialization in the context of safe software design.
- [Type inference guidance](guidance/type_inference.md): provides guidance on when to rely on type inference in the Clockwork language and when to use explicit types.

## Getting help

Check out the [Clockwork troubleshooting guide](troubleshooting.md) for help debugging and resolving common issues when building or deploying a Clockwork system.

## Migrating to the new reduced boilerplate Clockwork file format

- [Reduced boilerplate migration](reference/reduced_boilerplate_migration.md): provides guidance for migrating to the new reduced boilerplate Clockwork format.
