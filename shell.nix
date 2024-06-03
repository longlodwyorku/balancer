with import <nixpkgs> {};
mkShell {
  packages = with pkgs; [
    nodejs
    unzip
    bear
    gnumake
  ];
  shellHook = ''
    export USER=$(id -un)
    export UID=$(id -u)
  '';
  nativeBuildInputs = with pkgs; [];
  buildInputs = with pkgs; [];
}
