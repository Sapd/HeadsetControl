{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  packages = with pkgs; [
    cmake
    gcc
    hidapi
    llvmPackages_18.clang-tools
    ninja
    pkg-config
  ];
}
