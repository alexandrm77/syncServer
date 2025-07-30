import QtQuick 2.12
import QtQuick.Controls 2.5

ApplicationWindow {
    visible: true
    width: 400
    height: 300
    title: "Mode Switch Test"

    Column {
        anchors.centerIn: parent
        spacing: 20

        Button {
            text: "Switch to Server"
            onClicked: syncController.switchToMode(1) // Mode::Server
        }

        Button {
            text: "Switch to Client"
            onClicked: syncController.switchToMode(2) // Mode::Client
        }

        Button {
            text: "Switch to None"
            onClicked: syncController.switchToMode(0) // Mode::None
        }

        Text {
            text: "Current mode: " + syncController.mode
        }
    }
}
