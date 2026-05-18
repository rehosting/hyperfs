# List of overlays to be applied for cross-compiled packages
[

  # Disable libfuse's /etc/mtab handling with util-linux's mount command
  (self: super: {
    fuse3 = super.fuse3.overrideAttrs (o: {
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

  # GnuTLS' docs build runs generated target binaries such as lt-errcodes,
  # which fails when cross-compiling.  Disable the build and also drop the
  # devdoc output, otherwise nix fails with "failed to produce output path
  # for output 'devdoc'" since the directory is never created.
  (self: super: {
    gnutls = super.gnutls.overrideAttrs (o: {
      configureFlags = (o.configureFlags or [ ]) ++ [ "--disable-doc" ];
      outputs = builtins.filter (x: x != "devdoc") (o.outputs or [ "out" ]);
    });
  })

  # Fix musl+loongarch+gdb build
  # https://www.openwall.com/lists/musl/2024/08/02/1
  (self: super: {
    musl = super.musl.overrideAttrs (o: {
      patches = o.patches or [ ] ++ [ ./patches/musl-loongarch-regset.patch ];
    });
  })

  # Disable unused and/or broken-on-some-platforms elfutils features
  (self: super: {
    elfutils = super.elfutils.override { enableDebuginfod = false; };
  })

]
