/* -*- mode: html -*- */
/**

\page notes Design notes

\section nested_functions Nested functions

Nested functions should be implemented at a higher level using the
\ref insn_cast instruction and \ref instructions_lifting "lifting"
instructions to transfer contextual type information from one function
to another.

\section type_variables

Need to add a way of introducing type variables in the low-level
assembler after dereferencing a pointer. The \ref insn_cast and \ref
pointer_offset instructions should also have their \ref type_class
parameters removed and replaced by direct type references: it's
impossible to be totally consistent in syntax at the assembler level.

 */
