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

  iglooName = pkg:
    if pkg ? iglooName
    then pkg.iglooName
    else pkgs.lib.getName pkg;

  buildAllArchs = drvs:
    let
      pkgArchPairs = pkgs.lib.cartesianProductOfSets {
        pkg = drvs;
        arch = builtins.attrNames archs;
      };
    in pkgs.linkFarm "build-all-archs" (map ({ pkg, arch }: {
      name = "${iglooName pkg}.${arch}";
      path = pkgs.lib.getExe (buildPkgArch pkg arch);
    }) pkgArchPairs);

in buildAllArchs
