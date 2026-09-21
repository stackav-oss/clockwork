# States

Clockwork cogs are stateless, but sometimes it is necessary to store some information between executions of a cog.
States provide this framework, allowing for shared state that persists between cog executions.

## Creating states

States are created inside of a `box` and connected to one or more cogs.

```clockwork
#[cpp(type_namespace=”example::state”, type_header=”path/to/custom_state.hh”)]
extern_type CustomState;

box ExampleBox
{
  new init_cog: ExampleInitCog;
  new schema_state: State(representation=Tachyon<ExampleSchema>, init=init_cog.schema_state);

  new state_memory: HeapMemory(max_size=1024);
  new schema_state: State(representation=CustomState, memory_resource=state_memory, init=init_cog.extern_state);
}
```

As shown in the above example, there are three arguments that can be provided when creating a state:

- `representation`: The underlying data type for a state (shown above)
- `memory_resource`: If needed, a [memory resource](memory_resources.md) to use with the state
- `init`: Optional init cog state endpoint to connect to

### Representation

Every state needs to have an underlying representation.
Clockwork supports two types of representation: Clockwork schemas and user-provided C++ classes.

Clockwork schemas are the simplest way to specify the state's representation.
When a schema is specified as the representation, Clockwork manages the state internally similar to a single-item channel.

You can also opt to provide a custom C++ class instance to use as a state representation.
Clockwork will manage allocating the resource with the provided memory resource and make it available on the cog's dial.
The C++ state representation must provide a constructor with a single memory resource argument.
The memory resource connected to the state will be passed to the state on construction.

```cpp
namespace example::state
{
class CustomState
{
public:
    CustomState(jewels::memory::MemoryResource memory_resource) :
        state_vector_{memory_resource}
    {}

private:
    std::pmr::vector state_vector_;
};
}
```

Clockwork's [state snapshot and restore](state_snapshot_and_restore.md) only works with Schema representations.

### Memory resources

States that are represented by a C++ type **must** have a memory resource specified on creation.
Memory resources are required for Clockwork to manage the state's memory usage independent from any cogs that interact with it.
If a memory resource is missing, the Clockwork compiler will raise an error.

Conversely, states that are represented by Clockwork schemas **must not** specify a memory resource.
Clockwork handles memory allocation for Tachyon state similar to how it handles memory for channels, so no additional memory resources are needed.
Specifying a memory resource for a Tachyon state will result in a Clockwork compiler error.

### Initialization

Clockwork can automatically connect a state to an init cog when the state is created.
See the [composition](../concepts/composition.md) docs for more details around init cogs and state initialization.

## Connecting states

Cogs connect to state objects using endpoints defined in the states block of their cog definition.
Each state in the cog needs to be defined with its interface.
The following options need to be specified for each state:

- `mutable`: Whether the cog is allowed to mutate the state instance.

```clockwork
#[cpp(type_namespace=”example::state”, type_header=”path/to/custom_state.hh”)]
extern_type CustomState;

// Example cog to illustrate state connections
cog ExampleCog
{
  states
  {
    cog_state: Tappy<ExampleStateSchema>
    {
        mutable: true;
    }
    external_state: CustomState
    {
        mutable: true;
    }
  }
  execution
  {
    execute when: init;
  }
}

box ExampleBox
{
  new cog: ExampleCog;
  new state_memory_resource: HeapMemory(max_size=4096);
  new cpp_state: State(representation=CustomState, memory_resource=state_memory_resource, init=cog.external_state);

  new state: State(representation=Tachyon<ExampleStateSchema>);
  connect state to cog.cog_state;
}
```

In the above example, `cpp_state` uses the `init` parameter to implicitly connect to the cog's state endpoint.
`state` is declared and connected separately.

## Using states

References to each connected state are provided on the cog's dial.
An accessor for each state is generated with the state endpoint's name in the cog definition.
The mutability of each state on the dial is determined by the `mutable` option in the cog's state endpoint definition.

```cpp
void execute_cog(ExampleCogDial& dial)
{
    auto& extern_state = dial.get_states().get_cpp_state();
    auto& cog_state = dial.get_states().get_cog_state();
}
```
