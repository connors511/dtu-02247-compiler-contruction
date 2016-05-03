# Limitations
Comparison is performed using look-ahead.
That is, line X is compared to line Y with X < Y.
This results in error messages that assumes line Y is wrong, thus makes suggestions based on X being correct.

At this point, the pass is only able to check for string arguments and single literals, i.e. compare "add(a, a)" to "add(a, b)".
Comparison between complex types such as "a+b" or "a*a" will results in messages containing LLVM IR. The same goes for integers.

The complex arguments could be compared, if a auxiliary method was created to stringify the arguments.
That is, convert any argument ("2", 2, a+2, a+(b-c) etc) to a string such that these could be compared.
This however, takes a bit of work, as there's a lot of types in LLVM, each representing the different variable types.

The pass only works on a single function at a time, as its implemented as a FunctionPass.
However, one of the very first things the method does, is to grab the Functions parent (the module) in order to fetch similar function calls.
I imagine, that it'd be quite simple to reverse the process -- that is, to start from a ModulePass and then for each function in the module, ask the initial module for similar calls.



# Report 4
At this point, the detector can analyse differences in call arguments for strings, integers and single literals and hint to the user if anything looks like its out of place.
Complex literals i.e. arithmic expressions are just checked for equality and IR is outputted directly to the user if they're different and deemed a mistake.
The pass currently takes one function at a time (FunctionPass) and for each instruction looks up other occurences of that instruction via Module::getFunction(functionName) and then compares the two.


# Future work
I had a few difficulties getting the variable name from a load instruction such that I could compare function arguments in `add(a, a)' vs `add(a, b)'. This section is my thoughts on how I'd extend the pass, had I time.

## Improving detection
Handling arithmic expressions (such as a+b and a*a) could be solved by creating a method that could take a Value as input and convert it to a string representation -- this would make it easier to handle complex cases than the current code.
That is, to convert `%5 = add nsw i32 %3, %4, !dbg !22` to `a+a` so that the program could make a string comparison for literals.
Doing so would allow checking complex arguments with nested arithmic operations as well.

Pseudo code:

processExpression(value):
  sequence <- initialize
  visit(sequence, value)
  generateSprintfCallFromSequence(sequence)

visit(sequence, value):
  if value is load:
    sequence.add(load.pointer)
  else if value is binaryop:
    sequence.add(openingParen)
    visit(sequence, binaryop.operand(0))
    sequence.add(binaryop.opcode)
    visit(sequence, binaryop.operand(1))
    sequence.add(closingParen)

Adding the opening and closing paranthesis would be needed to distinguish `(b+c-d)*e` from `b+(c-d)*e`.

## Detecting blocks
Detecting blocks can be done in two ways.
The first is to introduce some ``lookahead'' functionality to the current pass. That is, to increment both pointers (I and Inst) as long as they're the same and then trigger a message when encountering a line that is just similar or way off.
The other way is to encode or serialize parts of the code as a graph and compare graphs to find blocks of duplicate code.
