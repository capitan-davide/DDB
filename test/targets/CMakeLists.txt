add_executable(run-endlessly RunEndlessly.cpp)
target_compile_options(run-endlessly PRIVATE -g -O0 -pie)

add_executable(end-immediately EndImmediately.cpp)
target_compile_options(end-immediately PRIVATE -g -O0 -pie)

add_executable(hello-ddb HelloDDB.cpp)
target_compile_options(hello-ddb PRIVATE -g -O0 -pie)

add_executable(reg-read RegRead.S)
target_compile_options(reg-read PRIVATE -pie)

add_executable(reg-write RegWrite.S)
target_compile_options(reg-write PRIVATE -pie)

add_dependencies(tests
  run-endlessly
  end-immediately
  hello-ddb
  reg-read
  reg-write
)
