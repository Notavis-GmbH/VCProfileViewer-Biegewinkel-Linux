# VC3DProfileViewer v2.0 – Build-Anleitung

## Voraussetzungen

| Werkzeug | Version | Download |
|----------|---------|---------|
| Qt       | 6.x oder 5.15.x | https://www.qt.io/download-open-source |
| MSVC     | 2022 (x64) | Visual Studio 2022 Build Tools |
| CMake    | ≥ 3.16 | im Qt-Installer enthalten |

**Wichtig:** Im Qt-Installer sicherstellen, dass **Qt Charts** mitinstalliert wird  
(unter Components → Qt 6.x → Qt Charts).

---

## Option A – Qt Creator (empfohlen)

1. Qt Creator öffnen  
2. **Datei → Projekt öffnen** → `CMakeLists.txt` auswählen  
3. Als Kit: **Desktop Qt 6.x MSVC2022 64bit** wählen  
4. Build-Konfiguration: **Release**  
5. **Strg+B** → bauen  
6. EXE liegt in `<build-ordner>/bin/Release/VC3DProfileViewer.exe`

---

## Option B – Kommandozeile (Developer Command Prompt for VS 2022)

```bat
cd C:\Pfad\zu\VC3DProfileViewer
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
cmake --build . --config Release
```

EXE liegt danach in `build\bin\Release\`.

---

## Deployment – portable Ordner erstellen

Nach dem Build-Erfolg im Developer Command Prompt:

```bat
cd build\bin\Release

:: Alle Qt-DLLs, Plugins, etc. automatisch kopieren
windeployqt VC3DProfileViewer.exe

:: Unterordner anlegen (werden auch automatisch beim ersten Start erstellt)
mkdir Data
mkdir Devices
```

Der fertige Ordner sieht so aus:

```
VC3DProfileViewer\
├── VC3DProfileViewer.exe
├── Qt6Core.dll
├── Qt6Gui.dll
├── Qt6Widgets.dll
├── Qt6Charts.dll
├── Qt6Network.dll
├── Qt6OpenGL.dll          (falls benötigt)
├── platforms\
│   └── qwindows.dll
├── Data\                  ← JSON-Aufzeichnungen hier ablegen
├── Devices\               ← default.ini wird beim 1. Start automatisch erstellt
└── README.txt
```

Diesen Ordner kann man auf einen USB-Stick kopieren und auf beliebigen  
Windows-Rechnern (64-bit) ausführen – **ohne Installation**.

---

## Sensor-Hinweis

Falls der Sensor zuvor mit SmartShape verbunden war, muss er einmal  
neu gestartet (Stromzufuhr trennen) werden, bevor VC3DProfileViewer  
sich verbinden kann. Alternativ Port **1097** in der UI verwenden.

---

## JSON-Wiedergabe

1. Im Bereich „Datenquelle" → **JSON Wiedergabe** auswählen  
2. Auf **„…"** klicken → Ordner mit `.json`-Dateien wählen  
   (Format: `LaserLineData_YYYYMMDD_HHMMSSxxxxxx.json`)  
3. Auf **▶ Play** klicken – die Frames werden in Dateinamensreihenfolge abgespielt  
4. Geschwindigkeit per Schieberegler anpassen (1 = 0.5 Hz, 5 ≈ 2 Hz, 10 = 10 Hz)  
5. **◀◀ / ▶▶** für manuellen Schritt vor/zurück

Die JSON-Dateien können auch im Unterordner `Data\` des Programmordners  
abgelegt werden – beim Öffnen des Dialogs wird dieser Ordner als Standard  
vorgeschlagen.
