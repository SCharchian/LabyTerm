import QtQuick
import Quickshell
import "Paths.js" as Paths

// Installs the launcher entry so LabyTerm shows up in SUPER+SPACE. Omarchy has
// no install hook for plugins, so the service does it while it is loaded and
// removes it again when the plugin is disabled or removed.
//
// Only a file carrying the X-LabyTerm-Managed marker is written or deleted.
QtObject {
  id: root

  property string omarchyPath: ""
  property var shell: null
  property var manifest: null

  readonly property string source: Paths.fromUrl(Qt.resolvedUrl("labyterm.desktop"))
  readonly property string launch: Paths.fromUrl(Qt.resolvedUrl("labyterm-launch"))
  readonly property string icon: Paths.fromUrl(Qt.resolvedUrl("labyterm.svg"))

  readonly property string dest: Quickshell.env("HOME") + "/.local/share/applications/labyterm.desktop"
  readonly property string marker: "^X-LabyTerm-Managed=true$"

  readonly property string installScript:
      '[ -f "$1" ] || exit 0\n'
    + 'if [ -e "$2" ] && ! grep -q "$3" "$2"; then exit 0; fi\n'
    + 'mkdir -p "${2%/*}" || exit 0\n'
    + 'tmp=$2.labyterm.new\n'
    + 'sed -e "s|@LAUNCH@|$4|" -e "s|@ICON@|$5|" "$1" > "$tmp" || exit 0\n'
    + 'if cmp -s "$tmp" "$2"; then rm -f "$tmp"; else mv -f "$tmp" "$2"; fi\n'

  readonly property string removeScript:
    'grep -q "$2" "$1" 2>/dev/null && rm -f "$1"\n'

  property bool installed: false

  Component.onCompleted: {
    if (source === "" || launch === "") return
    installed = true
    Quickshell.execDetached(["sh", "-c", installScript, "sh", source, dest, marker, launch, icon])
  }

  Component.onDestruction: {
    if (!installed) return
    Quickshell.execDetached(["sh", "-c", removeScript, "sh", dest, marker])
  }
}
