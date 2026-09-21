# Memory Resources

Memory resources are Clockwork-managed memory allocators that support monitoring and fixed allocation limits.
They can be used to provide custom allocation strategies for cogs and cog state.

## Types of memory resource

Clockwork provides the following types of memory resources in the DSL:

- `HeapMemory`: Resource that allocates on the heap, wrapping C++ `new` and `delete`.

## Creating memory resources

Memory resources are instantiated inside of a `box`, similar to cogs and states.
Memory resources must be declared with a `max_size` parameter indicating the maximum number of bytes that can be allocated through that resource.

```clockwork
box ExampleBox
{
  new heap_memory_resource: HeapMemory(max_size=1024);
}
```

## Connecting memory resources

Memory resources are used within Cogs and State objects.
Each memory resource may only be connected to one object.

### Cogs

Cogs can create a memory resource endpoint in the `resources` block of the cog definition.
Each memory resource endpoint can defined with the following types:

- `persistent`: Memory resource is persistent across cog executions.

In a box, a memory resource needs to be created and connected to the cog instance's endpoint.

```clockwork
// Example cog to illustrate memory resource connections
cog ExampleCog
{
  resources
  {
    cog_memory: persistent;
  }
  execution
  {
    execute when: init;
  }
}

box ExampleBox
{
  new cog: ExampleCog;
  new heap_memory: HeapMemory(max_size=1024);
  connect heap_memory to cog.cog_memory;
}
```

The cog can then access the memory resource in the cog dial during execution.

```cpp
void execute_cog(ExampleCogDial& dial)
{
    std::pmr::vector temp_vector{dial.get_resources().get_cog_memory()};
}
```

### States

States defined as external C++ types require a memory resource.
The memory resource is provided when the state is instantiated in a box.
[States](states.md) provides more details on when a memory resource is required for a state.

## Using a memory resource

Clockwork memory resources inherit the C++ `std::pmr::memory_resource` polymorphic memory resource.
The polymorphic memory resources are wrapped in the `jewels::memory::MemoryResource` class in the generated code.

You can use a Clockwork memory resource in the same way you'd use any `std::pmr::memory_resource`.
The easiest way to do this is through the `pmr` container aliases in the C++ standard library:

```cpp
jewels::memory::MemoryResource resource{std::pmr::new_delete_memory_resource()};
std::pmr::vector example_vector{{1, 2, 3, 4}, resource};
std::pmr::string example_string{"Hello, world!", resource};
```
