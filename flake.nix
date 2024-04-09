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
            path = "lib/libshimguin.so";
            env = "gnu";
            isStatic = false;
          }
        ];
        default = all-archs;
      };
      checks.x86_64-linux = rec {
        shimguin = pkgs.callPackage ./src/shimguin/test.nix { };
        shimguin-cross = buildAllArchs (map (env: {
            pkg = shimguin;
            path = "success";
            inherit env;
            isStatic = false;
          }) [ "gnu" "musl" "uclibc" ]);
      };
    };
}
