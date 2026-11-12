pragma Singleton
import QtQuick

// Every color, size and font of the app lives in this one file (a "singleton": there is exactly one Theme,
// and any QML file can write Theme.accent without creating it).
// To change the look later, change the numbers here and nothing else.
QtObject {

    // ---- colors ----
    readonly property color page: "#F6F9F7"        // the window background (a very light green-grey)
    readonly property color card: "#FFFFFF"        // cards and rows sit on the page in white
    readonly property color border: "#E1E8E3"      // thin lines around cards
    readonly property color text: "#14201A"        // main text (nearly black, with a hint of green)
    readonly property color textMuted: "#55655C"   // secondary text
    readonly property color accent: "#15803D"      // the green: buttons, progress, links
    readonly property color accentHover: "#166534" // darker green for hover and pressed
    readonly property color accentSoft: "#E6F4EA"  // pale green backgrounds
    readonly property color accentText: "#166534"  // green text that sits on accentSoft (dark enough to read)
    readonly property color textOnAccent: "#FFFFFF"    // text on top of the green
    readonly property color danger: "#B91C1C"      // errors
    readonly property color dangerSoft: "#FDECEC"  // pale red background for errors
    readonly property color track: "#D5E0D8"       // the empty part of a progress bar

    // ---- spacing scale: use these instead of raw numbers, so everything lines up ----
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 24
    readonly property int space6: 32

    // ---- shape ----
    readonly property int radius: 12        // cards
    readonly property int radiusSmall: 8    // buttons and fields
    readonly property int maxContentWidth: 760 // on wide windows the content stops growing here and stays centered

    // ---- fonts ----
    readonly property string fontFamily: "Inter"
    readonly property int fontSmall: 12
    readonly property int fontBody: 14
    readonly property int fontTitle: 18
    readonly property int fontHeading: 26
}
