This is my implementation of [Lox](http://craftinginterpreters.com/)/VM (thus "vlox",
because I implemented the AST based one in C++ instead of Java, and that was "clox"...),
with all the challenges I went through, plus some extensions.

It's a work in progress, really.

Included extras:

- Implemented most of the challenges in the original book, meaning indices are not
  limited to 8 bits any longer, among other things.
- Integers are a separate type, mostly for use as indexes.
- List support, based on [Caleb Schoepp's suggestions](https://calebschoepp.com/blog/2020/adding-a-list-data-type-to-lox/). Additionally:
  - Support for negative indexing
  - Support for slicing using the same syntax and conventions as Python.
    - Assignment to slices still pending.
  - Made append/delete into keywords (for the time being):
    * `append expression newelement` - where expression returns a list.
    * `delete identifier[expression]` - where identifier must refer to a list.
  - Deleting enough elements from a string makes it shrink. The current
    implementation is a bit naive though (just halve its capacity whenever
    possible).
- Generalized indexing and slicing so that it works on strings.
