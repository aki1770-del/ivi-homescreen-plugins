# Flatpak plugin [WIP]

### Status

* Applications can be installed
* Progress is displayed during installation
* Applications can be started in sandbox

### Ubuntu Package Dependency

```bash
sudo apt install libflatpak-dev libxml2-dev zlib1g-dev
```

### Fedora Runtime Packages

```bash
sudo dnf install flatpak-devel libxml2-devel
```

### Example flatpak CLI usage

```bash
flatpak remotes
flatpak list
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak remote-ls
flatpak install org.gnome.Todo
flatpak run org.gnome.Todo
```

### Flutter code

https://github.com/toyota-connected/tcna-packages/tree/v2.0/packages/flatpak

### Yocto Notes

* _Requires meta-flutter and meta-gnome_

* The toyota-connected-tcna-packages-flatpak-flutter-example recipe has a runtime dependency on `flatpak` and `xdg-desktop-portal-gnome`.

* To enable `xwayland` for weston, include `x11 wayland` in DISTRO_FEATURES.  This will allow any app to run (x11/wayland).

* You will likely need to resize your root partition to fit your storage drive.  This is best done on the birthday boot; saves on image size.

* conf/local.conf
```
IMAGE_INSTALL:append = " \
    toyota-connected-tcna-packages-flatpak-flutter-example
"    
```

Set locale for App runtime.  Using the default of `C` will not work with some apps.
```bash
# localectl set-locale C.UTF-8
# localectl
```
* Requires logging out and back in for the `LANG` environmental variable to update.

### Install/verify a Flathub app using Flatpak

* If you’re running under a minimal Weston session (user: `weston`), ensure there’s a user session bus before running the checks.
```bash
eval `dbus-launch --sh-syntax`
```

* Wayland only
```bash
mkdir -p /home/weston/.local/share/flatpak/exports/share
export XDG_DATA_DIRS=/home/weston/.local/share/flatpak/exports/share:$XDG_DATA_DIRS
```

* X11/Wayland
```bash
eval `dbus-launch --sh-syntax`
flatpak --user remote-add flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak --user install org.telegram.desktop -y
flatpak --user run org.telegram.desktop
```

### Launch Flutter Flatpak app
```bash
eval `dbus-launch --sh-syntax`
flatpak --user update --appstream
SPDLOG_LEVEL=debug homescreen -b /usr/share/flutter/ toyota-connected-tcna-packages-flatpak-flutter-example/3.35.7/release/
```

### Troubleshooting: org.freedesktop.portal.Settings error

If you see errors like:

```text
qt.qpa.theme.gnome: dbus reply error: [ "org.freedesktop.DBus.Error.UnknownMethod" ] "No such interface “org.freedesktop.portal.Settings” on object at path /org/freedesktop/portal/desktop"
```

It means the XDG Desktop Portal on your session bus doesn’t provide the Settings interface. This usually happens when the portal or a matching backend isn’t installed/running, or the portal is too old.

Verify the portal provides the Settings interface:

```bash
# Ensure a session bus exists (use dbus-launch if needed, as shown above)
busctl --user introspect org.freedesktop.portal.Desktop \
    /org/freedesktop/portal/desktop org.freedesktop.portal.Settings

# Alternative
gdbus introspect --session \
    --dest org.freedesktop.portal.Desktop \
    --object-path /org/freedesktop/portal/desktop | grep -i Settings || true
```

Flatpak sandbox notes:

* Portals run on the host. To check from inside a Flatpak:

```bash
flatpak-spawn --host busctl --user introspect \
    org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop \
    org.freedesktop.portal.Settings
```

### Generate message.g.h and messages.g.cc

    git clone https://github.com/toyota-connected/tcna-packages/tree/v2.0/packages/flatpak
    cd packages/flatpak
    dart run pigeon --input pigeons/messages.dart

### Flatpak API reference

https://docs.flatpak.org/en/latest/libflatpak-api-reference.html
