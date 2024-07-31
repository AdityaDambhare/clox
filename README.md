This is a bytecode interpreter for the toy language lox written in c from the book [crafting interpreters](https://craftinginterpreters.com/) by bob nystorm 

<h3>Changes from the original interpreter </h3>
<ul>
  <li>Added new Operators . The power(^) , c-style comma(,) and c-style ternary operator</li>
  <li> Used computed gotos in the vm implementation </li>
  <li> increased the limit for maximum constants in a chunk from 256 to 65000 </li>
  <li> increased the max stack frames allowed from 256 to 1024 </li>
  <li> made some changes to runtime error reporting and gc debugging </li>
  <li> added break and continue statements for loops </li>
</ul>
<h3> todo (increasing in difficulty)</h3>
<ul>
  <li> add lists </li>
  <li> add dictionary </li>
  <li> <s>add function expressions </s></li>
  <li><s> add getters in class </s></li>
  <li>Add an optional cache directive for functions and methods</li>
  <li> use inline caches to speed up method and field lookup</li>
  <li> create a standard library or a c-api </li>
  <li> work on a jit implementation if possible </li>
</ul>

<h3> How to build and run </h3>

run make
```
make
```

this will generate object files in /obj folder and the final executable in /bin

run using
```
bin/clox
```