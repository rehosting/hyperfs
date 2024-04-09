{ nixpkgs, pkgs }:

let
  archs = import ./archs.nix;

  buildPkgArch =
    pkg: arch:
    let
      archPkgs = import nixpkgs {
        inherit (pkgs) system;
        crossSystem = archs.${arch};
      };
    in
    archPkgs.callPackage pkg.override { };

  buildAllArchs =
    pkgsPaths:
    let
      pkgArchPairs = pkgs.lib.cartesianProductOfSets {
        pkgPath = pkgsPaths;
        arch = builtins.attrNames archs;
      };
    in
    pkgs.linkFarm "build-all-archs" (
      map (
        { pkgPath, arch }:
        let
          normalizedPkgPath =
            if pkgs.lib.isDerivation pkgPath then
              {
                pkg = pkgPath;
                path = "bin/${pkgPath.meta.mainProgram}";
              }
            else
              pkgPath;
          inherit (normalizedPkgPath) pkg path;
        in
        {
          name = "${baseNameOf path}.${arch}";
          path = "${buildPkgArch pkg arch}/${path}";
        }
      ) pkgArchPairs
    );
in
buildAllArchs
