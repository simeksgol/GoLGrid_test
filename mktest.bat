gcc.exe test.c -O3 -o test256.exe -Wall -Wextra -Werror -fmax-errors=2 -Wno-unused-function -fno-tree-loop-distribute-patterns -march=haswell
gcc.exe test.c -O3 -o test128.exe -Wall -Wextra -Werror -fmax-errors=2 -Wno-unused-function -fno-tree-loop-distribute-patterns -march=core2 -D __NO_AVX2
