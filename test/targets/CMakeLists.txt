add_executable(run-endlessly RunEndlessly.cpp)

add_executable(end-immediately EndImmediately.cpp)

add_executable(reg-write RegWrite.S)
target_compile_options(reg-write PRIVATE -pie)

add_executable(reg-read RegRead.S)
target_compile_options(reg-read PRIVATE -pie)
