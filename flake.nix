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
    {
      packages.x86_64-linux =
        let

          system = "x86_64-linux";

          nixpkgs-patched =
            (import nixpkgs {
              inherit system;
            }).applyPatches
              {
                name = "nixpkgs-patched";
                src = nixpkgs;
                patches = [ ./src/nixpkgs-fix-ppc.patch ];
              };

          pkgs = import nixpkgs-patched {
            inherit system;
            config.allowUnsupportedSystem = true;
          };

        in
        rec {
          all-archs = import ./src/build-dist.nix { inherit self pkgs; } (pkgs: [
            (pkgs.bash // { iglooName = "bash-unwrapped"; })
            (import ./src/pkgs/strace.nix pkgs)
            (import ./src/pkgs/gdbserver.nix pkgs)
            (import ./src/pkgs/ltrace.nix pkgs)
            (import ./src/pkgs/micropython.nix pkgs)
          ]);

          default = all-archs;
        };
    };
}
