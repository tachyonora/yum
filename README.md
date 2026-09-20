## yum

## Dependencies

- [gnumake](https://ftp.gnu.org/old-gnu/Manuals/make-3.80/html_node/make.html)
- [gcc](https://gcc.gnu.org/)

## Installation

### Building

```sh
git clone https://github.com/tachyonora/yum.git
cd yum
make
```

Then add to your binaries:

#### System wide

```sh
sudo cp ./yum /usr/local/bin/yum
```

#### User only

```
cp ./yum ~/.local/bin/yum
```

### Nix

#### Running automatically with nix

```sh
nix run github:tachyonora/yum
```

#### Installing with flake

First, add this to your flake inputs:

```nix
yum = {
    url = "github:tachyonora/yum";
    inputs.nixpkgs.follows = "nixpkgs";
};
```

Then pass to your outputs:

```nix
outputs = { nixpkgs, yum, ... }:
```

And install on your `environment.systemPackages`:

```nix
inputs.yum.packages.${pkgs.stdenv.hostPlatform.system}.default
```

## Usage

Run without any argument to open the default editor dashboard where you can freely start typing:

```sh
yum
```

Use a file path as argument to open it:

```sh
yum /path/to/file
```

## Keybinds

| Keybind               | Description                     |
|-----------------------|---------------------------------|
| `Ctrl+Q`                | Exits yum without saving        |
| `Ctrl+S` </file/path>   | Save current file. If editing </br>a new file, use the new file </br> path as a argument   |
| `Ctrl+F`                | Search for a word inside current file   |
| arrows                | Navigate between search results |
| Escape/Enter          | Exit search mode                |
