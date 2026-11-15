# What is left

Everything below needs a real device or a real run; none of it can be settled by reading code.

1. **Run the GitHub workflows once** (Actions tab: CI on a push, then Release as a dry run) and fix whatever turns red. Windows and macOS are the likely ones.
2. **Try the Android debug APK on a phone** (the CI run attaches it as an artifact): turn on Receive, pair with a desktop, send both ways.
   Check in particular: the device shows up in the other side's nearby list, the notification appears, files land in `Downloads/OneNode`.
3. **Desktop screens on Windows and macOS:** look for font, scaling and file-dialog differences.
4. Your list of UI corrections for the desktop screens.
5. Later ideas, not started: IPv6 for device discovery, sending a folder, sending text, a QR code for pairing, an iOS app.
