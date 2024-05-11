{
  description = "Hypercall-based FUSE filesystem";

  inputs = {
    libhc = {
      url = "github:panda-re/libhc";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, libhc }: {
    packages.x86_64-linux = let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
      hyperfsFromPkgs = pkgs: pkgs.callPackage ./src/hyperfs { inherit libhc; };
      ltraceFromPkgs = pkgs: pkgs.callPackage ./src/ltrace { };
    in rec {
      hyperfs = hyperfsFromPkgs pkgs;
      ltrace = ltraceFromPkgs pkgs;
      all-archs = import ./src/build-all-archs.nix { inherit pkgs nixpkgs; }
        (pkgs: [
          (hyperfsFromPkgs pkgs)
          (ltraceFromPkgs pkgs)
          (pkgs.bash // { iglooName = "bash-unwrapped"; })
          (pkgs.strace.override { libunwind = null; })
          (pkgs.gdbHostCpuOnly.overrideAttrs (self: {
            # Fix for MIPS+musl
            postPatch = self.postPatch or "" + ''
              substituteInPlace gdb/mips-linux-nat.c \
                --replace '<sgidefs.h>' '<asm/sgidefs.h>' \
                --replace '_ABIO32' '1'
            '';
            meta.mainProgram = "gdbserver";
          }))
        ]);
      default = all-archs;
    };
  };
}
