# Python Cogs

This document details how Python Cogs are implemented and used within the Clockwork system.
Before jumping into the example, it's important to understand the following:

- When a Python Cog is declared, the Clockwork compiler will automatically generate the C++ Cog implementation and a Python Cog Dial for the Python Cog.
- The interface to the Python Dial is adapted from the C++ Dial.
- The Python implementation for each Cog is defined as a function named \`execute_cog\` that takes the generated Dial as a parameter.
- Everything that the Cog needs to execute is accessed through the Dial and output messages by calling publish methods on the Dial’s output objects.

> [!WARNING]
> Python Cogs do not possess many of the determinism guarantees one would require for a real-time system.
> As such their usage is discouraged for such systems.
> They are intended for offline use or cases where real-time properties are not required.

## Example

- The definition for ExampleCog defines a Cog that executes periodically or when a new input message is received using configuration and state defined in Clockwork.
- Every Python Cog must have a mutable state member named “python_state” defined as an external type “PythonState”.
- The Python class names for the Python Cog Dial and Python Cog implementation are specified in a "python_options" section.
- Trying to generate a python_cog for a Cog that does not have a "python_options" section will fail at compile time.

```python
use example::python_cog::config::Config
use example::python_cog::state::State
use example::python_cog::input_message::InputMessage
use example::python_cog::output_message::OutputMessage

extern_type PythonState;

cog ExampleCog
{
  configs
  {
    config : Tappy<Config>;
  }
  states
  {
    python_state: PythonState
    {
      mutable: true;
    }
    state: Tappy<State>
    {
      mutable: true;
    }
  }
  inputs
  {
    input_msg: Tappy<InputMessage>;
  }
  outputs
  {
    output_msg: Tappy<OutputMessage>;
  }
  execution
  {
    condition new_input_msg: new_message(input_msg, max=1);
    condition periodic_100ms: time_since_last_exec(100ms);
    execute when: periodic_100ms or new_input_msg;
  }
  python_options
  {
    dial: "example.python_cog.py_python_cog_dial.PythonCogDial";
    impl: "example.python_cog.py_python_cog_impl.PythonCogImpl";
  }
}
```

The PythonState type contains a dictionary that Python Cogs can use to store anything they want to persist between Cog executions.

```cpp
/// C++ state common to all python cogs
class PythonState
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit PythonState(jewels::memory::MemoryResource memory_resource);

  ~PythonState() = default;
  PythonState(const PythonState& other) = delete;
  PythonState& operator=(const PythonState& other) = delete;
  PythonState(PythonState&&) noexcept = default;
  PythonState& operator=(PythonState&&) noexcept = default;

  /// Python state dictionary accessor
  /// @return Python dictionary object
  [[nodiscard]] const PythonObject& get_python_state_dictionary() const;

private:
  /// Dictionary of state available to the python cog
  PythonObject python_state_dictionary_;
};
```

Generating the C++ and Python code for the Cog is done using cpp_target rules and python_target rules in Clockwork.

The cpp_target uses the “python_cog” statement to generate the C++ Cog, C++ Cog Dial, and an additional C++ Cog implementation that constructs a Python Cog Dial and calls the Python Cog implementation from the C++ “execute_cog” method.
The mapping from a Clockwork Cog to the classes that implement the Dial and implementation is not fixed, so the python_cog statement needs to provide the Python classes explicitly.

```python
cpp_target example_cog
{
  options
  {
    namespace example::python_cog;
  }

  extern
  {
    header_file: "platforms/clockwork/dial/python_state.hh";
    namespace: stack::clockwork;
    type
    {
        name: PythonState;
    }
  }

  python_cog ExampleCog;
}
```

An extra definition is needed in a Python target to generate the Python version of the Cog Dial.
There is extra complexity required because there is no simple way to get the Python import path for a Clockwork type.
This means that the python_cog_dial statement must provide the Python type for every config, state, input, and output used by the Cog (except for Python state).

The following block will generate the ExampleCogDial class used to represent the C++ Dial in Python.
Note that it is possible to mix native Python and nanobind C++ bindings in the same Dial.

```python
py_target py_example_cog_dial
{
  python_cog_dial ExampleCog
  {
    configs.config: example.python_cog.state_clk_nb.Config;
    states.state: example.python_cog.py_state.State;
    inputs.input_msg: example.python_cog.py_input_message.InputMessage;
    inputs.input_msg: example.python_cog.output_message_clk_nb.OutputMessage;
  }
}
```

To implement the Cog in Python the user just provides a class with an execute_cog method in an implementation class.
That implementation class name appends the “Impl” prefix onto the name of the Python Cog.
After each call to execute_cog the generated C++ implementation publishes any output messages that were published by the Python implementation and serializes any mutable state back into the C++ Cog state.

```python
from example.python_cog.py_example_cog_dial import ExampleCogDial
from example.python_cog.output_message_clk_nb import OutputMessage

class ExampleCogImpl
    """Example cog implementation."""

    @classmethod
    def execute_cog(cls, dial: ExampleCogDial) -> None:
        """Execute the example cog.

        Arguments:
            dial: Example cog dial.
        """
       config = dial.configs.config
       py_state = dial.states.python_state[“py_state”]
       state = dial.states.state
       ...

       for buffer in dial.inputs.input_msg.new_msg_view():
           msg_in = buffer.message # Lazy deserialization
           ...

       msg_out = OutputMessage()
       ...
       dial.outputs.output_msg.publish(msg_out)
```

Defining a Python Cog in a Box is done as if it were any other Cog.
There are no changes required in the Box definition to support Python.

```python
box ExampleBox
{
  new memory_resource: memory(max_size=1'000'000);
  new config: SerializedDataFile(...);
  new python_state: State(representation=PythonState, memory_resource=memory);
  new state: State(representation=State, memory_resource=memory);
  new my_cog: ExampleCog;
  connect config to my_cog.config;
  connect python_state to my_cog.python_state;
  connect state to my_cog.state;
  connect my_channels::ExampleInputChannel to my_cog.input_msg;
  connect my_cog.output_msg to my_channels::ExampleOutputChannel;
}
```

Defining the Python Cog in a casing is done by putting a python_cog definition in the casing section of a cpp_executable definition.

```python
cpp_executable
{
  casing
  {
    python_cog ExampleCog;
  }
}
```
