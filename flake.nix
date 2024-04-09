{
  description = "Hypercall-based FUSE filesystem";

  inputs = {
    libhc = {
      url = "github:panda-re/libhc";
      flake = false;
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      libhc,
    }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in
    {
      packages.x86_64-linux = rec {
        hyperfs = pkgs.callPackage (import ./src/hyperfs { inherit libhc; }) { };
        unionfs = pkgs.callPackage ./src/unionfs { };
        shimguin = pkgs.callPackage ./src/shimguin { };
        all-archs = import ./src/build-all-archs.nix { inherit pkgs nixpkgs; } [
          hyperfs
          unionfs
          pkgs.bash
          {
            pkg = shimguin;
            path = "lib/libshimguin.so";
          }
        ];
        default = all-archs;
      };
      checks.x86_64-linux = rec {
        shimguin = pkgs.callPackage ./src/shimguin/test.nix { };
        shimguin-cross = import ./src/build-all-archs.nix { inherit pkgs nixpkgs; } [
          {
            pkg = shimguin;
            path = "success";
          }
        ];
      };
    };
}
