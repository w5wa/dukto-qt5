// Vertical "slider" control used in colorpicker
import QtQuick 2.3
Item {
    id: root
    property real value: (1 - pickerCursor.y/height)
    width: 15; height: 300

    signal changed()

    function setValue(value) {
        if (value < 0)
            value = 0
        else if (value > 1)
            value = 1
        pickerCursor.y = (1 - value) * height;
    }

    Item {
        id: pickerCursor
        width: parent.width
        Rectangle {
            x: -3; y: -height*0.5
            width: parent.width + 4; height: 7
            border.color: "black"; border.width: 1
            color: "transparent"
            Rectangle {
                anchors.fill: parent; anchors.margins: 2
                border.color: "white"; border.width: 1
                color: "transparent"
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: containsMouse ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor
        function handleMouse(mouse) {
            if (mouse.buttons & Qt.LeftButton) {
                pickerCursor.y = Math.max(0, Math.min(height, mouse.y))
                root.changed();
            }
        }
        onPositionChanged: handleMouse(mouse)
        onPressed: handleMouse(mouse)
    }
}

