{ nixpkgs, pkgs }:

let
  archs = import ./archs.nix;

  archPkgs = arch:
    import nixpkgs {
      inherit (pkgs) system overlays;
      crossSystem = archs.${arch};
    };

  iglooName = pkg: pkg.iglooName or pkg.meta.mainProgram;

  buildAllArchs = drvs:
    let
      pkgArchPairs = pkgs.lib.flatten
        (map (arch: map (pkg: { inherit pkg arch; }) (drvs (archPkgs arch)))
          (builtins.attrNames archs));
    in pkgs.linkFarm "build-all-archs" (map ({ pkg, arch }: {
      name = "${iglooName pkg}.${arch}";
      path = pkgs.lib.getExe pkg;
    }) pkgArchPairs);
in buildAllArchs
