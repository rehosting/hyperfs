{
  lib,
  runCommandCC,
  fuse3,
  pkg-config,
  autoreconfHook,
}:

runCommandCC "hyperfs"
  {
    buildInputs = [ fuse3 ];
    nativeBuildInputs = [ pkg-config ];
    meta.mainProgram = "hyperfs";
  }
  ''
    mkdir -p $out/bin
    $CC -g \
      ${./.}/hyperfs.c \
      `$PKG_CONFIG fuse3 --cflags --libs` \
      -Wall -Wextra -Werror -Wno-sign-compare \
      -o $out/bin/hyperfs
  ''
