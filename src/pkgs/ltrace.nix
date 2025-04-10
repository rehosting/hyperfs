pkgs:

# Implementation of glibc's error() from error.h
# https://man7.org/linux/man-pages/man3/error.3.html
let
  errorImpl = ''
    #define _GNU_SOURCE
    #include <errno.h>
    #define error(status, errnum, ...) \
      do { \
        fflush(stdout); \
        fprintf(stderr, "%s: ", program_invocation_name); \
        fprintf(stderr, __VA_ARGS__); \
        if (errnum != 0) { \
          fprintf(stderr, ": %s", strerror(errnum)); \
        } \
        fprintf(stderr, "\n"); \
        if (status != 0) { \
          exit(status); \
        } \
      } while (0)
  '';

in
pkgs.ltrace.overrideAttrs (prev: {

  patches = prev.patches or [ ] ++ [
    # Allow musl in configure script
    (pkgs.fetchpatch {
      url = "https://raw.githubusercontent.com/openembedded/meta-openembedded/f804417cda245e073c38fbdd6749e0bd49a1c84d/meta-oe/recipes-devtools/ltrace/ltrace/0001-configure-Recognise-linux-musl-as-a-host-OS.patch";
      sha256 = "sha256-CoMYbe7Cf4eO8UadCEhApm5r0vwyiyjPNCbwD/H2Mxg=";
    })
  ];

  postPatch =
    prev.postPatch or ""
    + ''
      # Add missing unistd.h.
      # Without this, when building for x86_64 there is a compile error about
      # pid_t being undefined.
      printf "#include <unistd.h>\n%s" "$(cat proc.h)" > proc.h

      # Comment out the line starting with `lte->relplt_count`.
      substituteInPlace sysdeps/linux-gnu/mips/plt.c \
        --replace 'lte->relplt_count' '//' \

      # Musl doesn't have error.h, so replace the include with a macro
      # implementation of error().
      substituteInPlace sysdeps/linux-gnu/{mips/plt.c,ppc/regs.c} \
        --replace '#include <error.h>' ${pkgs.lib.escapeShellArg errorImpl}
    '';

  # Ltrace should look for prototype files in /igloo/ltrace
  configureFlags = [ "--datadir=/igloo" ];

  # We don't want `make install` to actually try to install to /igloo/ltrace,
  # since the build sandbox doesn't have permission to write there
  installFlags = [ "datadir=$(out)/share" ];

  CFLAGS = "-Wno-format-overflow";

  doCheck = false;

  # Ltrace doesn't support mips64eb.
  # There is a patch, but it doesn't support this version.
  # https://github.com/openembedded/meta-openembedded/blob/master/meta-oe/recipes-devtools/ltrace/ltrace/0001-Add-support-for-mips64-n32-n64.patch
  passthru.iglooExcludedArchs = [
    "mips64eb"
    "mips64el"
    "riscv64"
    "riscv32"
    "loongarch"
    "ppc64"
    "ppc64el"
  ];
})
