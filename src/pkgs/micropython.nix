pkgs:

let

  libs = [ "fcntl" "os" ];

  manifest = pkgs.writeText "manifest.py" (
    pkgs.lib.concatMapStrings (lib: ''
      require('${lib}', unix_ffi=True)
    '') libs
  );
in
pkgs.micropython.overrideAttrs {
  buildPhase = ''
    export PATH=$PWD/bin:$PATH
    mkdir bin

    ln -fs ${pkgs.buildPackages.buildPackages.gcc}/bin/gcc bin/gcc
    ln -fs ${pkgs.buildPackages.buildPackages.binutils}/bin/strip bin/strip
    ln -fs ${pkgs.buildPackages.buildPackages.binutils}/bin/size bin/size
    ln -fs ${pkgs.buildPackages.buildPackages.pkg-config}/bin/pkg-config bin/pkg-config
    make -C mpy-cross

    ln -fs $(command -v $CC) bin/gcc
    ln -fs $(command -v $STRIP) bin/strip
    ln -fs $(command -v $SIZE) bin/size
    ln -fs $(command -v $PKG_CONFIG) bin/pkg-config
    make FROZEN_MANIFEST=${manifest} -C ports/unix
  '';
  NIX_CFLAGS_COMPILE = "-Wno-cpp -Wno-clobbered -Wl,--allow-multiple-definition -DMICROPY_NLR_SETJMP=1";
  doCheck = false;
  meta.mainProgram = "micropython";
}
