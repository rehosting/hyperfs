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

  # OpenSSL's 04-test_bio_dgram.t fails in restricted CI sandboxes that lack
  # proper DGRAM/IPv6 loopback.  Skip the test phase rather than carry an
  # upstream-specific patch.
  (self: super: {
    openssl = super.openssl.overrideAttrs { doCheck = false; };
  })

  # GnuTLS' docs build runs generated target binaries such as lt-errcodes,
  # which fails when cross-compiling.  Disable the build and also drop the
  # devdoc/man outputs, otherwise nix fails with "failed to produce output
  # path for output 'devdoc'" / "'man'" since the directories are never
  # created.
  (self: super: {
    gnutls = super.gnutls.overrideAttrs (o: {
      configureFlags = (o.configureFlags or [ ]) ++ [ "--disable-doc" ];
      outputs = builtins.filter
        (x: !(builtins.elem x [ "devdoc" "man" ]))
        (o.outputs or [ "out" ]);
    });
  })

  # Fix musl+loongarch+gdb build
  # https://www.openwall.com/lists/musl/2024/08/02/1
  (self: super: {
    musl = super.musl.overrideAttrs (o: {
      patches = o.patches or [ ] ++ [ ./patches/musl-loongarch-regset.patch ];
    });
  })

  # Disable unused and/or broken-on-some-platforms elfutils features, and
  # ensure pkg-config is available at build time (the configure script
  # needs it to locate zlib/bzip2/lzma/zstd; for cross builds nixpkgs
  # doesn't always inject it automatically).
  (self: super: {
    elfutils = (super.elfutils.override { enableDebuginfod = false; }).overrideAttrs (o: {
      nativeBuildInputs = (o.nativeBuildInputs or [ ]) ++ [ self.pkg-config ];
    });
  })

]
