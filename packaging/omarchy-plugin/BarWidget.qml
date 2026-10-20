import QtQuick
import Quickshell
import Quickshell.Io
import qs.Ui
import qs.Commons
import "Paths.js" as Paths

// LabyTerm bar widget. One click does the obvious thing: start the game, hide it
// (it is a drop-down on a Hyprland special workspace) or show and focus it again.
// The window is docked under the bar at this widget. The game quits only from inside the game. The icon dims when stopped and gets an accent bar when showing, which is
// polled from `labyterm-toggle --status` (stopped / hidden / visible). The widget only ever controls the drop-down;
// a game started from the launcher is a separate window that it does not touch.

BarWidget {
  id: root
  moduleName: "io.github.scharchian.labyterm"

  readonly property string toggleScript: Paths.fromUrl(Qt.resolvedUrl("labyterm-toggle"))

  property string gameState: "stopped"
  readonly property string status: "LabyTerm"   // the tooltip: the same in every state, the click always does the obvious thing

  function toggle(redock) {
    if (!root.bar || toggleScript === "") return
    // Dock the window under this widget: pass the widget's center in screen columns (the bar spans the screen).
    var win = root.QsWindow.window
    var p = win ? root.mapToItem(win.contentItem, root.width / 2, 0) : null
    var args = p ? [toggleScript, "--anchor", String(Math.round(p.x))] : [toggleScript]
    if (redock) args.push("--dock")
    Quickshell.execDetached(args)
    // Re-read soon so the icon follows the click without waiting for the poll.
    refreshSoon.restart()
  }

  Process {
    id: poll
    command: [root.toggleScript, "--status"]
    stdout: StdioCollector {
      onStreamFinished: {
        var s = text.trim()
        if (s === "stopped" || s === "hidden" || s === "visible") root.gameState = s
      }
    }
  }
  function refresh() { if (toggleScript !== "" && !poll.running) poll.running = true }

  Timer { interval: 2000; running: true; repeat: true; triggeredOnStart: true; onTriggered: root.refresh() }
  Timer { id: refreshSoon; interval: 600; onTriggered: root.refresh() }

  // Click anywhere outside the drop-down (and the bar) while it is showing: hide it, like the shell's own popups.
  // `--hide` only hides, so a click that arrives just after the drop-down was already hidden never shows it again.
  DismissOverlay {
    active: root.gameState === "visible"
    geometryScript: root.toggleScript
    onDismissed: { Quickshell.execDetached([root.toggleScript, "--hide"]); refreshSoon.restart() }
  }

  readonly property int iconSize: Math.round(barSize * 0.7)

  implicitWidth: iconSize + Style.space(14)
  implicitHeight: barSize

  // The game's own icon (the same SVG the launcher entry uses). Rasterized at twice the size so it stays crisp on HiDPI.
  Image {
    id: glyph
    anchors.centerIn: parent
    width: root.iconSize
    height: root.iconSize
    source: Qt.resolvedUrl("labyterm.svg")
    sourceSize: Qt.size(root.iconSize * 2, root.iconSize * 2)
    smooth: true
    mipmap: true
    opacity: root.gameState === "stopped" ? 0.5 : 1.0
  }

  // A small accent bar under the icon while the game is showing (replaces the old accent-colored glyph).
  Rectangle {
    visible: root.gameState === "visible"
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Math.round((root.barSize - root.iconSize) / 4)
    width: root.iconSize * 0.5
    height: 2
    radius: 1
    color: Color.accent
  }

  // The bar only shows a tooltip for a target that reports `tooltipHovered` (as its own WidgetButton does).
  readonly property bool tooltipHovered: mouseArea.containsMouse

  MouseArea {
    id: mouseArea
    anchors.fill: parent
    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor
    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
    onClicked: function(mouse) { root.toggle(mouse.button === Qt.MiddleButton) }
    onEntered: if (root.bar) root.bar.showTooltip(root, root.status)
    onExited: if (root.bar) root.bar.hideTooltip(root)
  }
}
