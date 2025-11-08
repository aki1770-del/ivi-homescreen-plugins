# Flatpak plugin [WIP]

### Status

* Applications can be installed
* Progress is display during installation
* Applications can be started in sandbox

### Ubuntu Package Dependency

```
sudo apt install libflatpak-dev libxml2-dev zlib1g-dev
```

### Fedora Runtime Packages

```
sudo dnf install flatpak-devel libxml2-devel
```

### Example flatpak CLI usage

```
flatpak remotes
flatpak list
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak remote-ls
flatpak install org.gnome.Todo
flatpak run org.gnome.Todo
```

### Flutter code

https://github.com/toyota-connected/tcna-packages/tree/v2.0/packages/flatpak

### Generate message.g.h and messages.g.cc

    git clone https://github.com/toyota-connected/tcna-packages/tree/v2.0/packages/flatpak
    cd packages/flatpak
    dart run pigeon --input pigeons/messages.dart

### Flatpak API reference

https://docs.flatpak.org/en/latest/libflatpak-api-reference.html
