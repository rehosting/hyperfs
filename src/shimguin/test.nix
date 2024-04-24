{ pkgs, altLibcPkgs }:

assert pkgs.stdenv.hostPlatform.canExecute altLibcPkgs.stdenv.hostPlatform;

let

  unwrappedEmulator = pkgs.stdenv.hostPlatform.emulator pkgs.buildPackages;

  # Wrapper around `unwrappedEmulator` that abstracts over setting environment
  # variables
  emulator = env:
    let
      envList = pkgs.lib.mapAttrsToList
        (var: val: pkgs.lib.escapeShellArg "${var}=${val}")
        env;
      envLocal = pkgs.lib.concatStringsSep " " envList;
      envQemu = pkgs.lib.concatStringsSep " " (map (e: "-E ${e}") envList);
    in
      if pkgs.stdenv.hostPlatform.canExecute pkgs.stdenv.buildPlatform
      then "env ${envLocal}"
      else "${unwrappedEmulator} ${envQemu}";

  shimguin = pkgs.callPackage ./. { };

in altLibcPkgs.runCommandCC "shimguin-test" { inherit shimguin unwrappedEmulator; # TODO
                                            } ''
  set -x

  cat > test.c << EOF
    void abort(const char *);

    int main(void) {
      abort("Hello world!");
    }
  EOF

  cat > test-lib.c << EOF
    #include <stdlib.h>

    void my_abort(const char *s) {
      (void)s;
      abort();
    }
  EOF

  cat > test-dlopen.c << EOF
    #include <dlfcn.h>

    int main(void) {
      void *o = dlopen("./test-lib.so", RTLD_LAZY);
      void (*f)(const char *) = dlsym(o, "my_abort");
      f("Hello from dlopen!");
    }
  EOF

  $CC \
    -Wall -Wextra -Werror -Wno-builtin-declaration-mismatch \
    test.c -o test
  $CC -Wall -Wextra -Werror -shared test-lib.c -o test-lib.so
  $CC -Wall -Wextra -Werror test-dlopen.c -o test-dlopen

  # Ensure aborts when run normally
  ! ${emulator {}} ./test
  ${emulator {}} ./test || [ $(kill -l $(($? - 128))) = ABRT ]

  # Ensure prints string when abort() replaced with puts()
  [ "$(${emulator {
    SHIMGUIN_SHIMS = "abort->puts";
    LD_PRELOAD = "${shimguin}/lib/libshimguin.so";
  }} ./test)" = "Hello world!" ]

  # Same test, but for shimming a my_abort() function loaded with dlopen() after libshimguin

  # Ensure aborts when run normally
  ! ${emulator {}} ./test-dlopen
  ${emulator {}} ./test-dlopen || [ $(kill -l $(($? - 128))) = ABRT ]

  # Ensure prints string when abort() replaced with puts()
  [ "$(${emulator {
    SHIMGUIN_SHIMS = "my_abort->puts";
    LD_PRELOAD = "${shimguin}/lib/libshimguin.so";
  }} ./test-dlopen)" = "Hello from dlopen!" ]

  :> $out
''
