# Clockwork Public Standard Library

This directory contains public entities intended to be used within user code, primarily things intended to be used inside the DSL itself.
Examples of public DSL entities would be policy definitions provided by Clockwork.
Within the DSL, these would be imported with `use @clockwork::std::...`.

Non-DSL libraries that are closely related to DSL entities may also be placed here, though the core C++ and Python APIs (e.g. cog Dials) are located in `@clockwork//clockwork/...`.

At the moment not all entities that _should_ be here are here yet, because there are some legacy policies defined elsewhere.
Over time we will migrate those, and new public entities should go here.
