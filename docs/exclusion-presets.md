# App exclusion presets

Replay excludes its own window (`omarchy-replay`) and Omarchy's screensaver (`org.omarchy.screensaver`) on every installation. Fresh configurations also exclude the Steam client and the password and authentication apps below. You can remove those optional entries in Settings.

To add a group to an existing installation, open **Settings → Exclusions**, choose a preset, select **Add preset**, then **Save**. Adding a preset keeps your current entries and removes duplicates. Cancel leaves the saved configuration unchanged.

An explicit `[exclusions] apps` list replaces the defaults. Updates do not silently add entries to that saved list. Replay and the screensaver remain excluded even with `apps = []`.

## Passwords & authentication

These entries are included in fresh configurations and in the **Passwords & authentication** preset.

| App | Exact identifiers | Evidence |
| --- | --- | --- |
| 1Password | `com.onepassword.OnePassword`, `1password`, `1Password` | [Omarchy's app rules](https://github.com/basecamp/omarchy/blob/master/default/hypr/apps/1password.lua) cover the current and older identities. |
| Bitwarden | `Bitwarden`, `bitwarden` | [Omarchy's app rules](https://github.com/basecamp/omarchy/blob/master/default/hypr/apps/bitwarden.lua) and the [upstream runtime package](https://github.com/bitwarden/clients/blob/main/apps/desktop/src/package.json), which sets `desktopName` to `bitwarden.desktop`. |
| KeePassXC | `KeePassXC`, `org.keepassxc.KeePassXC` | [Upstream startup code](https://github.com/keepassxreboot/keepassxc/blob/develop/src/main.cpp) sets the application name and Wayland desktop filename. |
| Proton Pass | `Proton Pass` | The [upstream package](https://github.com/ProtonMail/WebClients/blob/main/applications/pass-desktop/package.json) supplies the product name used by [Electron's startup code](https://github.com/electron/electron/blob/v39.8.10/lib/browser/init.ts) and [Linux window code](https://github.com/electron/electron/blob/v39.8.10/shell/browser/native_window_views.cc). |
| Enpass | `Enpass` | The desktop entry in the [official Linux package](https://apt.enpass.io/pool/main/e/enpass/enpass_6.12.6.2258_amd64.deb) declares `StartupWMClass=Enpass`. |
| QtPass | `QtPass`, `qtpass` | [Upstream startup code](https://github.com/IJHack/QtPass/blob/main/main/main.cpp) sets the application name and desktop filename. Flatpak can override the latter. |
| GNOME Secrets | `org.gnome.World.Secrets` | [Build configuration](https://gitlab.gnome.org/World/secrets/-/blob/master/meson.build) supplies the ID used by the [application](https://gitlab.gnome.org/World/secrets/-/blob/master/gsecrets/application.py). |
| GNOME Authenticator | `com.belmoussaoui.Authenticator` | [Build configuration](https://gitlab.gnome.org/World/Authenticator/-/blob/master/meson.build#L43) supplies the ID used by the [GTK application](https://world.pages.gitlab.gnome.org/Authenticator/src/authenticator/application.rs.html#326-329). |
| OTPClient | `com.github.paolostivanin.OTPClient` | [Upstream application constructor](https://github.com/paolostivanin/OTPClient/blob/master/src/gui/otpclient-application.c#L156-L163) sets this ID. |
| Yubico Authenticator | `com.yubico.yubioath` | [Linux build configuration](https://github.com/Yubico/yubioath-flutter/blob/main/linux/CMakeLists.txt#L4-L5) supplies the ID used by [GTK startup](https://github.com/Yubico/yubioath-flutter/blob/main/linux/my_application.cc#L103-L113). |
| Passwords and Keys (Seahorse) | `org.gnome.Seahorse`, `seahorse`, `Seahorse` | [Current startup code](https://gitlab.gnome.org/GNOME/seahorse/-/blob/main/src/application.vala#L62-94) sets the application ID and process name. Older GTK3 builds use the [executable name](https://github.com/GNOME/seahorse/blob/master/data/org.gnome.seahorse.Application.desktop.in.in#L6), with [GTK's class capitalization](https://github.com/GNOME/gtk/blob/gtk-3-24/gdk/gdk.c#L1057-L1064) on X11. |

## Gaming apps

The Steam client is excluded by default. The **Gaming apps** preset also adds the other apps in this table.

| App | Exact identifiers | Evidence |
| --- | --- | --- |
| Steam client | `steam`, `Steam` | [Omarchy's app rules](https://github.com/basecamp/omarchy/blob/master/default/hypr/apps/steam.lua) use `steam`; a [Steam client report](https://github.com/ValveSoftware/steam-for-linux/issues/13102) identifies both variants. |
| Lutris | `net.lutris.Lutris` | [Upstream startup code](https://github.com/lutris/lutris/blob/master/lutris/gui/application.py) sets both the GTK application ID and `GLib` program name. GTK3 uses that program name for Wayland; the [desktop entry](https://github.com/lutris/lutris/blob/master/share/applications/net.lutris.Lutris.desktop) also declares this `StartupWMClass`. |
| Heroic | `heroic` | [Upstream desktop entry](https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher/blob/main/flatpak/com.heroicgameslauncher.hgl.desktop) declares `StartupWMClass=heroic`. |
| RetroArch | `com.libretro.RetroArch` | [Omarchy's app rules](https://github.com/basecamp/omarchy/blob/master/default/hypr/apps/retroarch.lua). |
| Moonlight | `com.moonlight_stream.Moonlight` | [Omarchy's app rules](https://github.com/basecamp/omarchy/blob/master/default/hypr/apps/moonlight.lua). |

Games launched through Steam, Lutris or Heroic can have separate identifiers. Add those windows individually with **Choose visible app…**; excluding a launcher does not exclude every game it starts.

## Media players

The optional **Media players** preset adds `mpv` and `vlc`. mpv's [desktop entry](https://github.com/mpv-player/mpv/blob/master/etc/mpv.desktop) declares `StartupWMClass=mpv`. VLC's [Qt startup code](https://github.com/videolan/vlc/blob/master/modules/gui/qt/qt.cpp) sets its desktop filename from the [package name `vlc`](https://github.com/videolan/vlc/blob/master/configure.ac).

## Matching and limits

Replay compares each exact, case-sensitive identifier with both the window's current and initial app identity. These presets cover the listed identities, not every package, wrapper or development build. The evidence combines upstream source, desktop entries and Omarchy rules; it does not establish live capture coverage for every app. Use **Choose visible app…** to check a different build locally.

Browser extensions and password-manager pages inside an ordinary browser window are not covered by these native app entries. A title rule or a separate browser-app identity may be needed; **Copy exclusions prompt** can help your coding agent configure it.

An excluded app pauses capture while potentially visible on the recorded display, even if it is unfocused. Replay's own window is masked without pausing capture. The compositor masks can also hide matching windows from other screen-sharing tools while Replay is stopped. Exclusions apply to future captures and do not delete existing history.

See [recording and configuration](background-recording.md#exclusions) for window rules, TOML configuration and reload behavior.
