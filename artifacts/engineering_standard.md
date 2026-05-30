# Embedded Engineering Standard & Development Guidelines (e-up!Proxy)

Dieses Dokument definiert den verbindlichen Qualitäts- und Entwicklungsstandard für die Arbeit am `e-up!Proxy` und zukünftigen Embedded-Projekten. Es dient als "indelebiles Gedächtnis", um systematisch-deterministisches Software-Engineering sicherzustellen und reaktiven "Trial & Error"-Modus (Herumprobieren) konsequent zu verhindern.

---

## 1. Die drei Säulen des Embedded-Engineering-Standards

### 1.1. Säule 1: Rigide Kontrollfluss- und Abhängigkeitsanalyse (Order of Init)
Embedded-Systeme sind streng sequentiell. Fehlerhafte Initialisierungsreihenfolgen führen zu unvorhersehbarem Verhalten, stillschweigendem Scheitern oder plötzlichen Abstürzen (z. B. Watchdog-Resets).

*   **Der Standard**: Vor jeder Änderung am Systemstart (`setup()`) oder an Hardware-Peripherien muss die logische Kette der Abhängigkeiten explizit abgebildet werden.
*   **Die goldene Initialisierungskette**:
    $$\text{1. Low-Level Hardware / NVS} \rightarrow \text{2. Peripherie (Pins, LEDs)} \rightarrow \text{3. Dateisystem (LittleFS)} \rightarrow \text{4. Log-System} \rightarrow \text{5. Konfiguration / Update-Checks} \rightarrow \text{6. Netzwerk (WiFi, Client-AP)}$$
*   **Regel**: Keine Funktion darf eine Ressource der Kette nutzen, die in einem früheren Schritt noch nicht vollständig initialisiert und verifiziert wurde.

### 1.2. Säule 2: High-Fidelity Mocking (Keine Maskierung im Test)
Unit-Tests und Simulatoren müssen die physikalischen Grenzen und logischen Zustände der echten Hardware so originalgetreu wie möglich abbilden.

*   **Der Standard**: Mocks dürfen Zugriffe im "unprepared" Zustand nicht stillschweigend durchwinken oder vereinfachen.
*   **Regel**:
    *   Wird eine Datei-Operation im Mock aufgerufen, obwohl das simulierte Dateisystem nicht gemountet ist, **muss** der Mock einen Fehler werfen oder der Test fehlschlagen.
    *   Wird ein Socket-Zugriff simuliert, ohne dass die simulierte WiFi-Verbindung steht, muss dies im Mock fehlschlagen.
    *   Mocks müssen aktiv Assert-Fehler erzeugen, wenn die logische Kette der Hardware-Voraussetzungen verletzt wird.

### 1.3. Säule 3: Log-First-Policy (Keine Hypothese ohne Daten)
Änderungen am Code zur Fehlerbehebung dürfen niemals auf "Vermutungen" oder "Bauchgefühl" basieren. Jede Modifikation muss datenbasiert bewiesen sein.

*   **Der Standard**: Der Kreislauf der Fehlerbehebung läuft deterministisch ab:
    1.  **Messen & Auslesen**: Vollständiges Auslesen der Logdateien (`/debug.log`, `/mqtt.log`, `/obd.log`) oder der seriellen Konsole.
    2.  **Verifizieren**: Eindeutige Identifikation der Codezeile oder des Systemzustands, der den Fehler verursacht.
    3.  **Hypothese beweisen**: Logischer Nachweis, warum die vorgeschlagene Änderung das Problem behebt und keine Regressionen in der Initialisierungskette erzeugt.
    4.  **Implementieren & Testen**: Code ändern, Unit-Tests ausführen und den Erfolg durch erneutes Log-Auslesen verifizieren.

---

## 2. Praktische Umsetzung im Workflow

### 2.1. Vorbereitung bei Code-Modifikationen
Wenn ein Feature oder Bugfix geplant wird, prüfen wir vorab:
1.  Welche Hardware-Ressourcen werden berührt?
2.  Gibt es Timing-Abhängigkeiten oder blockierende Aufrufe (z. B. WLAN-Scans)?
3.  Sind die Mocks im Test-Verzeichnis (`test/mocks/`) ausreichend detailliert, um das Verhalten abzubilden?

### 2.2. Versionierung als Kontrollwerkzeug
Jede funktionale oder strukturelle Änderung am Code führt zwingend zu einer Erhöhung der Minor- oder Patch-Version in `include/version.h` (z. B. von `2.3.1` auf `2.3.2`).
*   Dies zwingt den ESP32 beim ersten Start nach dem Flash dazu, den Speicher sauber zu reorganisieren und alte Logdateien zu purgen, wodurch wir immer in einer definierten, sauberen Umgebung testen.
