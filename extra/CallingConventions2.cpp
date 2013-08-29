/**
 * CallingConvetions2.cpp
 * 
 * Contains prototypes of a bunch of different functions to see how certain types
 * and combinations of parameters are passed, either by running the native
 * compiler on this file and examining the resulting assembler or by running clang
 * and examining the resulting bitcode.
 * 
 * Example commands:
 * 
 * g++ -S -o- CallingConventions2.cpp
 * clang++ -emit-llvm -o- -S CallingConventions2.cpp
 */

// Should work for most platforms...
typedef long long int64_t;
typedef int int32_t;

struct Long2_Constructor {
  int64_t x, y;
  Long2_Constructor();
};

Long2_Constructor long2_constructor_1(Long2_Constructor x) {
  return x;
}

struct Long2_Destructor {
  int64_t x, y;
  ~Long2_Destructor();
};

Long2_Destructor long2_destructor_1(Long2_Destructor x) {
  return x;
}

struct Long2_Copy {
  int64_t x, y;
  Long2_Copy(const Long2_Copy&);
};

Long2_Copy long2_copy_1(Long2_Copy x) {
  return x;
}
