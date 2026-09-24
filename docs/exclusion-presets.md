# App exclusion presets

Replay excludes its own window (`omarchy-replay`) and Omarchy's screensaver (`org.omarchy.screensaver`) on every installation. Fresh configurations skip the Steam client in Replay and hide the password and authentication apps below from screenshots and sharing. You can remove those optional entries in Settings.

To add a group to an existing installation, open **Settings → Exclusions**, choose a preset, select **Add preset**, then **Save**. Adding a preset keeps your current entries and removes duplicates. Cancel leaves the saved configuration unchanged.

`[exclusions] apps` contains privacy masks; `skip_apps` contains recording-only skips. Updates preserve saved entries. A legacy file with `apps` and no `skip_apps` retains its existing behavior. Replay and the screensaver remain protected even with both lists empty. Across both lists, the limit is 64 entries; an overlap counts in each list.

## Passwords & authentication

These entries are included in fresh configurations and in the **Passwords & authentication** preset. They go into **Hide from screenshots and sharing** (`apps`).

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

The Steam client is skipped in Replay by default. The **Gaming apps** preset adds all apps in this table to **Skip in Replay** (`skip_apps`), without masking ordinary screenshots or screen sharing.

| App | Exact identifiers | Evidence |
| --- | --- | --- |
| Steam client | `steam`, `Steam` | [Omarchy's app rules](https://github.com/basecamp/omarchy/blob/master/default/hypr/apps/steam.lua) use `steam`; a [Steam client report](https://github.com/ValveSoftware/steam-for-linux/issues/13102) identifies both variants. |
| Lutris | `net.lutris.Lutris` | [Upstream startup code](https://github.com/lutris/lutris/blob/master/lutris/gui/application.py) sets both the GTK application ID and `GLib` program name. GTK3 uses that program name for Wayland; the [desktop entry](https://github.com/lutris/lutris/blob/master/share/applications/net.lutris.Lutris.desktop) also declares this `StartupWMClass`. |
| Heroic | `heroic` | [Upstream desktop entry](https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher/blob/main/flatpak/com.heroicgameslauncher.hgl.desktop) declares `StartupWMClass=heroic`. |
| RetroArch | `com.libretro.RetroArch` | [Omarchy's app rules](https://github.com/basecamp/omarchy/blob/master/default/hypr/apps/retroarch.lua). |
| Moonlight | `com.moonlight_stream.Moonlight` | [Omarchy's app rules](https://github.com/basecamp/omarchy/blob/master/default/hypr/apps/moonlight.lua). |

Games launched through Steam, Lutris or Heroic can have separate identifiers. Add those windows individually with **Choose visible app…**; excluding a launcher does not exclude every game it starts.

## Media players

The optional **Media players** preset adds `mpv` and `vlc` to **Skip in Replay** (`skip_apps`). Phone mirrors can also use `mpv`; leave it out if you want the mirror recorded. These skips do not block ordinary screenshots or sharing. mpv's [desktop entry](https://github.com/mpv-player/mpv/blob/master/etc/mpv.desktop) declares `StartupWMClass=mpv`. VLC's [Qt startup code](https://github.com/videolan/vlc/blob/master/modules/gui/qt/qt.cpp) sets its desktop filename from the [package name `vlc`](https://github.com/videolan/vlc/blob/master/configure.ac).

## Matching and limits

Replay compares each exact, case-sensitive identifier with both the window's current and initial app identity. These presets cover the listed identities, not every package, wrapper or development build. The evidence combines upstream source, desktop entries and Omarchy rules; it does not establish live capture coverage for every app. Use **Choose visible app…** to check a different build locally.

Browser extensions and password-manager pages inside an ordinary browser window are not covered by these native app entries. A title rule or a separate browser-app identity may be needed; **Copy exclusions prompt** can help your coding agent configure it.

Both kinds pause capture while a matching app is potentially visible on the recorded display, even if unfocused. Privacy masks (`apps` and window rules) also hide pixels from other capture tools, even while Replay is stopped. Recording-only skips rely on window metadata and pre/post capture checks; they are not a secrecy guarantee for closing animations or popups. Replay's own window is masked without pausing capture. An entry in both lists stays masked. Moving an existing mask to a skip requires deliberately removing its `apps` entry; adding a preset never weakens an existing mask. Exclusions affect future captures and do not delete existing history.

An incoming Meet/Zoom screen share belongs to the local browser or meeting window. Replay does not match app identities shown within that video. Meetings remain recordable at the normal interval unless a local exclusion or another capture gate blocks the selected display.

See [recording and configuration](background-recording.md#exclusions) for window rules, TOML configuration and reload behavior.
