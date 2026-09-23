#pragma once

#include <QStringList>

namespace replay {

// Exact runtime identities and evidence are listed in docs/exclusion-presets.md.
// Presets are additive editor actions; an explicit saved app list stays authoritative.
inline QStringList privacyAppExclusions() {
    return {"com.onepassword.OnePassword", "1password", "1Password",
        "Bitwarden", "bitwarden", "KeePassXC", "org.keepassxc.KeePassXC",
        "Proton Pass", "Enpass", "QtPass", "qtpass", "org.gnome.World.Secrets",
        "com.belmoussaoui.Authenticator", "com.github.paolostivanin.OTPClient",
        "com.yubico.yubioath", "org.gnome.Seahorse", "seahorse", "Seahorse"};
}

inline QStringList gamingAppExclusions() {
    return {"steam", "Steam", "net.lutris.Lutris", "heroic",
        "com.libretro.RetroArch", "com.moonlight_stream.Moonlight"};
}

inline QStringList mediaAppExclusions() {
    return {"mpv", "vlc"};
}

inline QStringList defaultAppExclusions() {
    QStringList apps{"omarchy-replay", "org.omarchy.screensaver"};
    apps.append(privacyAppExclusions());
    apps.append({"steam", "Steam"});
    return apps;
}

}  // namespace replay
