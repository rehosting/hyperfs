{ nixpkgs, pkgs }:

let
  archs = import ./archs.nix;

  buildPkgArch = pkg: arch:
    let
      archPkgs = import nixpkgs {
        inherit (pkgs) system;
        crossSystem = archs.${arch};
      };
    in archPkgs.callPackage pkg.override { };

  buildAllArchs = drvs:
    let
      pkgArchPairs = pkgs.lib.cartesianProductOfSets {
        pkg = drvs;
        arch = builtins.attrNames archs;
      };
    in pkgs.linkFarm "build-all-archs" (map ({ pkg, arch }: {
      name = "${pkgs.lib.getName pkg}.${arch}";
      path = pkgs.lib.getExe (buildPkgArch pkg arch);
    }) pkgArchPairs);

in buildAllArchs
