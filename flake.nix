{
  description = "Hypercall-based FUSE filesystem";

  inputs = {
    libhc = {
      url = "github:panda-re/libhc";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, libhc }: {
    packages.x86_64-linux = let pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in rec {
      hyperfs = pkgs.callPackage (import ./src/hyperfs { inherit libhc; }) { };
      unionfs = pkgs.callPackage ./src/unionfs { };
      all-archs = import ./src/build-all-archs.nix { inherit pkgs nixpkgs; } [
        hyperfs
        unionfs
      ];
      default = all-archs;
    };
  };
}
