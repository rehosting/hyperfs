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

      hyperfsFromPkgs = pkgs:
        pkgs.callPackage ./src/pkgs/hyperfs { inherit libhc; };

    in rec {
      hyperfs = hyperfsFromPkgs pkgs;

      all-archs = import ./src/build-dist.nix { inherit self pkgs; }
        (pkgs: [
          (hyperfsFromPkgs pkgs)
          (pkgs.bash // { iglooName = "bash-unwrapped"; })
          (pkgs.strace.override { libunwind = null; })
          (import ./src/pkgs/gdbserver.nix pkgs)
          (import ./src/pkgs/ltrace.nix pkgs)
          (import ./src/pkgs/micropython.nix pkgs)
        ]);

      default = (import pkgs.path {
        inherit (pkgs) system;
        crossOverlays = import ./src/cross-overlays.nix;
        crossSystem = (import ./src/archs.nix).mipseb;
      }).fetchFromGitHub {
        owner = "micropython";
        repo = "micropython";
        rev = "v1.22.2";
        fetchSubmodules = true;
        sha256 = "sha256-6mQrBoda87UJsZ7M/NDF86Ergz2glQz3yje4zdBM+Bo=";
      };
    };
  };
}
