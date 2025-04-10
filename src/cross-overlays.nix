# List of overlays to be applied for cross-compiled packages
[

  # Disable libfuse's /etc/mtab handling with util-linux's mount command
  (self: super: {
    fuse3 = super.fuse3.overrideAttrs
      (o: {
        # The disable-mtab option is ignored
        # (https://github.com/libfuse/libfuse/issues/456),
        # so also disable mtab manually.
        mesonFlags = o.mesonFlags ++ [ "-Ddisable-mtab=true" ];
        CFLAGS = "-DIGNORE_MTAB=1";
      });
  })

  # Remove now-unneeded util-linux dependency to speed up build
  (self: super: {
    fuse3 = super.fuse3.override { util-linux = super.emptyDirectory; };
  })

  # The p11-kit tests seem to fail for single-user Nix installs
  (self: super: {
    p11-kit = super.p11-kit.overrideAttrs { doCheck = false; };
  })

  # Add musl support for loongarch64 and powerpc
  (self: super: {
    musl = if builtins.any (x: x == super.stdenv.hostPlatform.config) [
        "loongarch64-linux-musl"
        "powerpc-linux-musl"
      ]
      then super.musl.overrideAttrs (oldAttrs: {
        meta = oldAttrs.meta // {
          platforms = oldAttrs.meta.platforms ++ [
            "loongarch64-linux"
            "powerpc-linux"
          ];
          badPlatforms = [];
        };
      })
      else super.musl;
  })
  
  # Add linux-headers support for powerpc-linux
  (self: super: {
    linux-headers = super.linux-headers.overrideAttrs (oldAttrs: {
      meta = oldAttrs.meta // {
        platforms = oldAttrs.meta.platforms ++ [ "powerpc-linux" ];
      };
    });
  })
  
  # Add bash support for powerpc-linux-musl and other missing platforms
  (self: super: {
    bash = super.bash.overrideAttrs (oldAttrs: {
      meta = oldAttrs.meta // {
        platforms = oldAttrs.meta.platforms ++ [
          "powerpc-linux"
          "powerpc-linux-musl"
        ];
      };
    });
  })

]
