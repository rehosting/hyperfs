{
  stdenv,
  buildPackages,
  runCommandCC,
  callPackage,
}:

let
  emulator = stdenv.hostPlatform.emulator buildPackages;
  shimguin = callPackage ./. { };
in
runCommandCC "shimguin-test" { } ''
  set -x

  cat > test.c << EOF
    void abort(const char *);
    int main(void) {
      abort("Hello world!");
    }
  EOF

  $CC \
    -Wall -Wextra -Werror -Wno-builtin-declaration-mismatch \
    test.c -o test

  # Ensure aborts when run normally
  ! ${emulator} ./test
  ${emulator} ./test || [ $(kill -l $(($? - 128))) = ABRT ]

  # Ensure prints string when abort() replaced with puts()
  [ \
    "$( \
      SHIMGUIN_SHIMS='abort->puts' ${emulator} \
        $(patchelf --print-interpreter test) \
        --preload ${shimguin}/bin/libshimguin.so \
        ./test \
    )" = "Hello world!" \
  ]

  mkdir $out
  :> $out/success
''
