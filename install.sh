#!/bin/bash
# ============================================================
#  VC3DProfileViewer – Installer für Raspberry Pi 5 (ARM64)
#  Notavis GmbH
#
#  Verwendung:
#    curl -sL https://raw.githubusercontent.com/Notavis-GmbH/VCProfileViewer-Basis/main/install.sh | bash
#    curl -sL https://raw.githubusercontent.com/Notavis-GmbH/VCProfileViewer-KI/main/install.sh | bash
#    curl -sL https://raw.githubusercontent.com/Notavis-GmbH/VCProfileViewer-Falz/main/install.sh | bash
#
#  Update: Installer erneut ausführen – prüft Version und aktualisiert automatisch.
# ============================================================

set -euo pipefail

# ── Konfiguration (wird beim Einspielen ins Repo angepasst) ──────────────────
REPO_OWNER="Notavis-GmbH"
REPO_NAME="VCProfileViewer-Biegewinkel-Linux"          # Wird pro Repo gesetzt
APP_NAME="VC3DProfileViewer"
INSTALL_DIR="/opt/notavis/${REPO_NAME}"
DESKTOP_ENTRY="/usr/local/bin/vcprofileviewer"

# ── Farben ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; BOLD='\033[1m'; NC='\033[0m'

log()     { echo -e "${BLUE}[INFO]${NC}  $*"; }
ok()      { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

# ── Voraussetzungen prüfen ────────────────────────────────────────────────────
check_requirements() {
    log "Prüfe Voraussetzungen ..."
    for cmd in curl tar jq; do
        if ! command -v "$cmd" &>/dev/null; then
            warn "$cmd nicht gefunden – wird installiert ..."
            sudo apt-get install -y "$cmd" -qq
        fi
    done

    ARCH=$(uname -m)
    if [[ "$ARCH" != "aarch64" && "$ARCH" != "arm64" ]]; then
        warn "Architektur $ARCH erkannt (erwartet aarch64). Fortfahren auf eigene Gefahr."
    else
        ok "Architektur: $ARCH"
    fi
}

# ── Neueste Release-Version holen ─────────────────────────────────────────────
get_latest_release() {
    log "Suche neueste Version von ${REPO_OWNER}/${REPO_NAME} ..."
    LATEST_JSON=$(curl -sfL \
        "https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases/latest" \
        -H "Accept: application/vnd.github.v3+json")

    LATEST_TAG=$(echo "$LATEST_JSON" | jq -r '.tag_name')
    ASSET_URL=$(echo "$LATEST_JSON" | jq -r \
        '.assets[] | select(.name | test("linux_arm64.*\\.tar\\.gz$")) | .browser_download_url' \
        | head -1)

    if [[ -z "$ASSET_URL" || "$ASSET_URL" == "null" ]]; then
        error "Kein Linux ARM64 Release-Asset gefunden. Noch kein CI-Build vorhanden?"
    fi

    ok "Neueste Version: ${LATEST_TAG}"
    ASSET_NAME=$(basename "$ASSET_URL")
}

# ── Versionsprüfung (Update-Check) ────────────────────────────────────────────
check_installed_version() {
    if [[ -f "${INSTALL_DIR}/VERSION.txt" ]]; then
        INSTALLED_COMMIT=$(grep "^Commit:" "${INSTALL_DIR}/VERSION.txt" | awk '{print $2}')
        log "Installierte Version: ${INSTALLED_COMMIT:-unbekannt}"
    else
        log "Keine vorherige Installation gefunden."
        INSTALLED_COMMIT=""
    fi
}

# ── Herunterladen und installieren ────────────────────────────────────────────
download_and_install() {
    TMP_DIR=$(mktemp -d)
    trap "rm -rf $TMP_DIR" EXIT

    log "Lade ${ASSET_NAME} ..."
    curl -L --progress-bar "$ASSET_URL" -o "${TMP_DIR}/${ASSET_NAME}"

    log "Installiere nach ${INSTALL_DIR} ..."
    sudo mkdir -p "$INSTALL_DIR"
    sudo tar -xzf "${TMP_DIR}/${ASSET_NAME}" -C "$INSTALL_DIR"
    sudo chmod +x "${INSTALL_DIR}/start.sh"
    sudo chmod +x "${INSTALL_DIR}/${APP_NAME}"

    # Verzeichnisse anlegen (falls nicht im Archiv)
    sudo mkdir -p "${INSTALL_DIR}"/{Data,Devices,TestData}

    ok "Dateien entpackt nach ${INSTALL_DIR}"
}

# ── Systemweiten Launcher erstellen ───────────────────────────────────────────
create_launcher() {
    log "Erstelle System-Launcher unter ${DESKTOP_ENTRY} ..."
    sudo tee "$DESKTOP_ENTRY" > /dev/null << EOF
#!/bin/bash
# VC3DProfileViewer Launcher – Notavis GmbH
export LD_LIBRARY_PATH="${INSTALL_DIR}/lib:\$LD_LIBRARY_PATH"
exec "${INSTALL_DIR}/${APP_NAME}" "\$@"
EOF
    sudo chmod +x "$DESKTOP_ENTRY"
    ok "Launcher erstellt: vcprofileviewer"
}

# ── Autostart (systemd User-Service, optional) ────────────────────────────────
create_autostart_service() {
    SERVICE_FILE="${HOME}/.config/systemd/user/vcprofileviewer.service"
    mkdir -p "$(dirname "$SERVICE_FILE")"

    cat > "$SERVICE_FILE" << EOF
[Unit]
Description=VC3DProfileViewer – Notavis GmbH
After=graphical-session.target

[Service]
Type=simple
Environment=DISPLAY=:0
Environment=LD_LIBRARY_PATH=${INSTALL_DIR}/lib
ExecStart=${INSTALL_DIR}/${APP_NAME}
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
EOF
    log "Autostart-Service erstellt (nicht aktiviert)."
    log "  Aktivieren:   systemctl --user enable --now vcprofileviewer"
    log "  Deaktivieren: systemctl --user disable vcprofileviewer"
}

# ── Update-Check ──────────────────────────────────────────────────────────────
needs_update() {
    if [[ -z "$INSTALLED_COMMIT" ]]; then
        return 0  # Keine Installation → Update nötig
    fi
    # Vergleiche ersten 7 Zeichen des Commits aus dem Release-Asset-Namen
    LATEST_COMMIT=$(echo "$ASSET_NAME" | grep -oE '[0-9a-f]{7}' | tail -1)
    if [[ "$LATEST_COMMIT" == "$INSTALLED_COMMIT" ]]; then
        return 1  # Gleiche Version → kein Update nötig
    fi
    return 0  # Anderer Commit → Update nötig
}

# ── Hauptprogramm ─────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}══════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}  VC3DProfileViewer Installer – Notavis GmbH          ${NC}"
echo -e "${BOLD}  Ziel: ${REPO_NAME}                               ${NC}"
echo -e "${BOLD}══════════════════════════════════════════════════════${NC}"
echo ""

check_requirements
get_latest_release
check_installed_version

if ! needs_update; then
    ok "Bereits aktuell (Commit: ${INSTALLED_COMMIT}). Kein Update nötig."
    echo ""
    echo -e "  Starten mit: ${BOLD}vcprofileviewer${NC}"
    echo ""
    exit 0
fi

download_and_install
create_launcher
create_autostart_service

echo ""
echo -e "${GREEN}${BOLD}Installation abgeschlossen!${NC}"
echo ""
echo -e "  Starten mit:  ${BOLD}vcprofileviewer${NC}"
echo -e "  Installiert:  ${INSTALL_DIR}"
echo -e "  Version:      ${LATEST_TAG}"
echo ""
echo -e "  Autostart aktivieren:"
echo -e "    ${BOLD}systemctl --user enable --now vcprofileviewer${NC}"
echo ""
