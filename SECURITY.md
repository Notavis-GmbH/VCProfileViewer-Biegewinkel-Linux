# Security Policy

## Geschützte Dateien

Die Datei `keygen_config.h` enthält den **Keygen.sh Product Token** und darf
**niemals** in dieses Repository committet werden. Sie ist in der `.gitignore`
eingetragen und muss lokal verwaltet werden.

| Datei | Inhalt | Status |
|---|---|---|
| `keygen_config.h` | Keygen Product Token, Policy-IDs, Account-ID | ❌ Niemals committen |

## Sicherheitshinweise

- Den Product Token **niemals** in öffentlichen Repos, Logs oder Chat-Tools teilen
- Bei Verdacht auf Token-Kompromittierung sofort unter [app.keygen.sh](https://app.keygen.sh) regenerieren und die neue `keygen_config.h` lokal aktualisieren
- Admin-Tokens von Keygen.sh haben vollständigen API-Zugriff — nur für Einrichtung verwenden, danach rotieren

## Sicherheitslücken melden

Sicherheitsprobleme bitte ausschließlich per E-Mail an: patrik.drexel@notavis.com
