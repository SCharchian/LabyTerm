import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland

// Click-away for the drop-down. While it is showing, a transparent overlay covers each screen, and a click on it
// asks for the drop-down to hide, like the shell's own popups. The overlay's input region leaves two places
// alone, so they keep working: the bar strip (the widget and the other bar items) and the game's own window.
// The game is a normal Hyprland window, not a shell surface, so its rectangle comes from `labyterm-toggle
// --geometry` and is refreshed while the drop-down is showing (the window can be moved and resized).
// The same approach as the shell's KeyboardPanel: an overlay layer-shell window per screen with a click mask.

Item {
  id: root

  property bool active: false          // the drop-down is showing
  property string geometryScript: ""   // path of labyterm-toggle
  signal dismissed()

  property var game: null              // {x, y, w, h} in layout pixels, or null
  property var monitors: []            // [{name, x, y, top}]
  property bool ready: false           // a first geometry has arrived: do not treat early clicks as click-away

  readonly property int slack: 4       // the window border is drawn outside the window's rectangle

  onActiveChanged: if (!active) ready = false

  Process {
    id: geo
    command: [root.geometryScript, "--geometry"]
    stdout: StdioCollector {
      onStreamFinished: {
        try {
          var o = JSON.parse(text)
          root.game = o.game
          root.monitors = o.monitors || []
          root.ready = true
        } catch (e) {}
      }
    }
  }

  Timer {
    interval: 250
    repeat: true
    running: root.active && root.geometryScript !== ""
    triggeredOnStart: true
    onTriggered: if (!geo.running) geo.running = true
  }

  Variants {
    model: root.active ? Quickshell.screens : []

    delegate: Component {
      PanelWindow {
        id: win
        required property var modelData

        screen: modelData
        color: "transparent"
        exclusionMode: ExclusionMode.Ignore

        WlrLayershell.namespace: "labyterm-dismiss"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

        anchors {
          top: true
          bottom: true
          left: true
          right: true
        }

        // This screen's entry in the monitor list (matched by output name), and the game's rectangle in this
        // screen's own coordinates.
        readonly property var mon: {
          for (var i = 0; i < root.monitors.length; i++)
            if (root.monitors[i].name === modelData.name) return root.monitors[i]
          return null
        }
        readonly property real barStrip: mon ? mon.top : 0
        readonly property bool hasGame: root.game !== null && mon !== null
        readonly property int gx: hasGame ? Math.round(root.game.x - mon.x - root.slack) : 0
        readonly property int gy: hasGame ? Math.round(root.game.y - mon.y - root.slack) : 0
        readonly property int gw: hasGame ? Math.round(root.game.w + 2 * root.slack) : 0
        readonly property int gh: hasGame ? Math.round(root.game.h + 2 * root.slack) : 0

        mask: Region {
          width: win.width
          height: win.height
          Region {
            x: 0
            y: 0
            width: win.width
            height: win.barStrip
            intersection: Intersection.Subtract
          }
          Region {
            x: win.gx
            y: win.gy
            width: win.gw
            height: win.gh
            intersection: Intersection.Subtract
          }
        }

        MouseArea {
          anchors.fill: parent
          acceptedButtons: Qt.AllButtons
          enabled: root.ready
          onPressed: root.dismissed()
        }
      }
    }
  }
}
