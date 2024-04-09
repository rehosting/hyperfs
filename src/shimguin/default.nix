{
  lib,
  rustPlatform,
  buildPackages,
  stdenv,
}:

rustPlatform.buildRustPackage {
  name = "shimguin";

  src = ./.;

  cargoLock.lockFile = ./Cargo.lock;

  LIBCLANG_PATH = "${buildPackages.libclang.lib}/lib";

  postConfigure = ''
    # https://hoverbear.org/blog/rust-bindgen-in-nix/
    export BINDGEN_EXTRA_CLANG_ARGS=" \
      $(< ${stdenv.cc}/nix-support/libc-crt1-cflags) \
      $(< ${stdenv.cc}/nix-support/libc-cflags) \
      $(< ${stdenv.cc}/nix-support/cc-cflags) \
      $(< ${stdenv.cc}/nix-support/libcxx-cxxflags) \
      ${lib.optionalString stdenv.cc.isClang "-idirafter ${stdenv.cc.cc}/lib/clang/${lib.getVersion stdenv.cc.cc}/include"} \
      ${lib.optionalString stdenv.cc.isGNU "-idirafter ${stdenv.cc.cc}/lib/gcc/${stdenv.hostPlatform.config}/${lib.getVersion stdenv.cc.cc}/include"} \
    "
  '';

  SHIMGUIN_SHIMS = ""; # For bindgen layout tests to not fail

  postInstall = "mv $out/{lib,bin}";

  meta.mainProgram = "libshimguin.so";
}
