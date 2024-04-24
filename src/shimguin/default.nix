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

  # Hack to prevent Rust from linking libgcc:
  # Replace cargo with a wrapper that uses a wrapped linker that filters out `-lgcc_s`.
  preBuild = ''
    mkdir bin
    cp ${buildPackages.writers.writePython3 "cargo-wrapper" { flakeIgnore = ["E501"]; } ''
      import os
      import sys

      os.environ["CARGO_TARGET_${stdenv.hostPlatform.rust.cargoEnvVarTarget}_LINKER"] = "${
        buildPackages.writers.writePython3 "link-wrapper" { flakeIgnore = ["E501"]; } ''
          import os
          import sys

          os.execvp(os.environ["CC"], [arg for arg in sys.argv if arg != "-lgcc_s"] + ["-Wl,--warn-unresolved-symbols"])
        ''
      }"
      os.execv("${lib.getExe buildPackages.cargo}", sys.argv)
    ''} bin/cargo
    export PATH=$PWD/bin:$PATH
  '';

  doCheck = false;

  postFixup = ''
    patchelf $out/lib/* --print-needed \
        | xargs printf -- '--remove-needed %s\n' \
        | xargs patchelf $out/lib/* --no-default-lib --remove-rpath
  '';
}
