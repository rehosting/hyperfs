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
      buildAllArchs = import ./src/build-all-archs.nix { inherit pkgs nixpkgs; };
    in
    {
      packages.x86_64-linux = rec {
        hyperfs = pkgs.callPackage (import ./src/hyperfs { inherit libhc; }) { };
        unionfs = pkgs.callPackage ./src/unionfs { };
        shimguin = pkgs.callPackage ./src/shimguin { };
        all-archs = buildAllArchs [
          hyperfs
          unionfs
          pkgs.bash
          {
            pkg = shimguin;
            path = "/lib/libshimguin.so";
            env = "gnu";
            isStatic = false;
          }
        ];
        default = all-archs;
      };
      checks.x86_64-linux = {
        shimguin = import ./src/shimguin/test.nix { inherit pkgs; altLibcPkgs = pkgs; };
        shimguin-cross = pkgs.writeText "shimguin-cross-test"
          (pkgs.lib.concatLines (
            map (
              { arch, env }:
              import ./src/shimguin/test.nix {
                pkgs = import ./src/arch-pkgs.nix { inherit pkgs nixpkgs; } arch { env = "gnu"; isStatic = false; };
                altLibcPkgs = import ./src/arch-pkgs.nix { inherit pkgs nixpkgs; } arch { inherit env; isStatic = false; };
              }
            ) (
              pkgs.lib.cartesianProductOfSets {
                arch = builtins.attrValues (import ./src/archs.nix);
                env = [
                  "gnu"
                  "musl"
                  "uclibc"
                ];
              }
            )
          ));
        dbg = import ./src/shimguin/test.nix {
         pkgs = import ./src/arch-pkgs.nix { inherit pkgs nixpkgs; } (import ./src/archs.nix).mips64eb { env = "gnu"; isStatic = false; };
         altLibcPkgs = import ./src/arch-pkgs.nix { inherit pkgs nixpkgs; } (import ./src/archs.nix).mips64eb { env = "musl"; isStatic = false; };
        };
      };
    };
}
