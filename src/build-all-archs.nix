{ nixpkgs, pkgs }:

let
  archs = import ./archs.nix;

  archPkgs = arch:
    import nixpkgs {
      inherit (pkgs) system;
      crossOverlays = import ./cross-overlays.nix;
      crossSystem = archs.${arch};
    };

  iglooName = pkg: pkg.iglooName or pkg.meta.mainProgram;

  buildUtils = drvs:
    let

      pkgArchPairs = builtins.filter
        ({ pkg, arch }: !builtins.elem arch pkg.iglooExcludedArchs or [ ])
        (pkgs.lib.flatten
          (map (arch: map (pkg: { inherit pkg arch; }) (drvs (archPkgs arch)))
            (builtins.attrNames archs)));
    in pkgs.linkFarm "utils" (map ({ pkg, arch }: {
      name = "${iglooName pkg}.${arch}";
      path = pkgs.lib.getExe pkg;
    }) pkgArchPairs);

  buildDist = drvs:
    let utils = buildUtils drvs;
    in pkgs.runCommand "dist" { } ''
      # Collect and copy libraries for the utils.
      # Also edit the interpreter and RPATH to be inside /igloo.
      for util in ${utils}/*; do

        arch=''${util##*.}
        mkdir -p $out/dylibs/$arch
        old_interp=$(patchelf $util --print-interpreter)

        echo "Collecting interpreter from $util"
        ln -sf $old_interp $out/dylibs/$arch/$(basename $old_interp)

        echo "Collecting library dependencies from $util"
        for lib_dir in $(IFS=:; echo $(patchelf --print-rpath $util)); do
          for so_name in $(patchelf --print-needed $util); do
            if [ -e $lib_dir/$so_name ]; then
              ln -sf $lib_dir/$so_name $out/dylibs/$arch/$so_name
            fi
          done
        done

        echo "Switching $util to IGLOO paths"
        mkdir -p $out/utils
        patchelf $util \
          --set-rpath /igloo/dylibs \
          --set-interpreter /igloo/dylibs/$(basename $old_interp) \
          --output $out/utils/$(basename $util)

      done
    '';
in buildDist
