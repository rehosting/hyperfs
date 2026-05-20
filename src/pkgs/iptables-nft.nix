pkgs:

# xtables-nft-multi is the multi-call binary for the nf_tables frontend.
# Override mainProgram so getExe resolves to the nft variant's symlink.
pkgs.iptables // {
  iglooName = "iptables-nft";
  meta = pkgs.iptables.meta // { mainProgram = "iptables-nft"; };
}
