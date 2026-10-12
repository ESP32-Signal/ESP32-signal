# GPIO-Belegung

Jede Liste gehört zu einem Baustein. Ein GPIO hat darin genau eine Funktion.

Tasten und Achszähler schließen nach Masse. High zieht ein Relais an. Die Status-LED an GPIO23 leuchtet aktiv High. Das Bahnhofsmodul schaltet die Weichen- und Signalrelais. Das Blockmodul schaltet seine Signalrelais nicht.

Paarungskabel, beide Stecker gleich: Pin 1 Masse, Pin 2 TX, Pin 3 RX. Das Kabel kreuzt TX und RX.

GPIO34 bis GPIO39 haben keinen internen Pull-up. Dort liegt 10 kΩ nach 3,3 V.

## Blockmodul

ESP32_Relay_X2. Firmware: `pio run -e block`.

| GPIO | Funktion | Anmerkung |
| --- | --- | --- |
| 0 | Boot-Taste | Kurz löscht die Paarung von Seite A, lang (700 ms) die von Seite B. Beim Reset gedrückt gehalten startet der Bootloader. |
| 1 | Konsole TX | UART0, 115200 8N1 |
| 3 | Konsole RX | UART0 |
| 14 | Paarung Seite A, RX | |
| 16 | Relais 1 | Blocksignal Seite A |
| 17 | Relais 2 | Blocksignal Seite B |
| 21 | OLED SDA | SSD1306 128×64, I2C-Adresse 0x3C, Versorgung 3,3 V |
| 22 | OLED SCL | |
| 23 | Status-LED | Dauerlicht bei Paarung und erreichbarem Nachbarn, sonst Blinken |
| 25 | Paarung Seite B, RX | |
| 26 | Paarung Seite B, TX | |
| 27 | Paarung Seite A, TX | |
| 32 | Achszähler Kontakt A | Liegt auf der Seite zur Buchse A. Interner Pull-up. |
| 33 | Achszähler Kontakt B | Liegt auf der Seite zur Buchse B. Interner Pull-up. |

## Bahnhofsmodul

ESP32_Relay_X8. Firmware: `pio run -e bahnhof`.

Der Bahnhof koppelt nur über Seite B. Das ist die Strecke. Seite A hat keine Buchse.

Vier Relais sind die Signalrelais, vier stellen Weichen. Anzeige 1 (Ausfahrt) hängt mit DIN am ESP32. Anzeige 2 (Einfahrt) bekommt DIN vom DOUT der ersten Anzeige, CLK und CS sind gemeinsam. Versorgung der Matrizen 5 V.

| GPIO | Funktion | Anmerkung |
| --- | --- | --- |
| 0 | Boot-Taste | Kurz löscht die Paarung von Seite A, lang (700 ms) die von Seite B. Beim Reset gedrückt gehalten startet der Bootloader. |
| 1 | Konsole TX | UART0, 115200 8N1 |
| 2 | MAX7219 DIN | Anzeige 1, Ausfahrt. Strap-Pin. |
| 3 | Konsole RX | UART0 |
| 4 | Gleistaste 1 | Interner Pull-up. Mit der Streckentaste wird die Ausfahrt beantragt. |
| 5 | Streckentaste | Interner Pull-up. Strap-Pin. Beim Reset nicht gedrückt halten. |
| 12 | Relais 7 | Weiche 3. Strap-Pin, beim Start Low lassen. |
| 13 | Relais 8 | Weiche 4 |
| 14 | Relais 6 | Weiche 2 |
| 15 | MAX7219 CLK | Ruhepegel Low. Strap-Pin. |
| 16 | MAX7219 CS | Ruhepegel Low |
| 17 | Paarung Seite B, TX | Zur Strecke |
| 18 | Gleistaste 3 | Interner Pull-up |
| 19 | Einfahrtsignaltaste | Interner Pull-up. Mit einer Gleistaste wird das Einfahrtsignal gestellt. |
| 21 | OLED SDA | SSD1306 128×64, I2C-Adresse 0x3C, Versorgung 3,3 V |
| 22 | OLED SCL | |
| 23 | Status-LED | Dauerlicht bei Paarung und erreichbarem Nachbarn, sonst Blinken |
| 25 | Relais 3 | Einfahrt Halt/Frei. Low ist Halt, High ist Frei |
| 26 | Relais 4 | Einfahrt gelb. High nur bei Frei 40 |
| 27 | Relais 5 | Weiche 1 |
| 32 | Relais 1 | Ausfahrt Halt/Frei. Low ist Halt, High ist Frei |
| 33 | Relais 2 | Ausfahrt gelb. High nur bei Frei 40 |
| 34 | Paarung Seite B, RX | 10 kΩ nach 3,3 V |
| 35 | Achszähler Kontakt A | 10 kΩ nach 3,3 V |
| 36 | Achszähler Kontakt B | Zur Strecke. 10 kΩ nach 3,3 V |
| 39 | Gleistaste 2 | 10 kΩ nach 3,3 V |

Stellung 0 ist die Gerade, Stellung 1 der Abzweig. Low am Relais ist Stellung 0, High ist Stellung 1. Weiche 1 und 2 bilden den Ausfahrkopf zur Strecke: Weiche 1 in 0 führt auf Gleis 1, in 1 auf Weiche 2. Weiche 2 in 0 führt auf Gleis 2, in 1 auf Gleis 3. Weiche 3 und 4 bilden den Einfahrkopf in derselben Weise. Eine Fahrstraße stellt nur die Weichen ihres Kopfes. Die Relais werden gelegt, bevor das Ein- oder Ausfahrtsignal auf Frei geht.

| Fahrstraße | Weiche 1 | Weiche 2 | Weiche 3 | Weiche 4 |
| --- | --- | --- | --- | --- |
| Ausfahrt Gleis 1 | 0 | bleibt | bleibt | bleibt |
| Ausfahrt Gleis 2 | 1 | 0 | bleibt | bleibt |
| Ausfahrt Gleis 3 | 1 | 1 | bleibt | bleibt |
| Einfahrt Gleis 1 | bleibt | bleibt | 0 | bleibt |
| Einfahrt Gleis 2 | bleibt | bleibt | 1 | 0 |
| Einfahrt Gleis 3 | bleibt | bleibt | 1 | 1 |

Die Webseite wählt je Fahrstraße Frei oder Frei 40. Das Umschaltrelais liegt bei Halt in Ruhe und zieht bei Frei und Frei 40 an. Das gelbe Relais zieht nur bei Frei 40 an. Voreingestellt ist Frei für Gleis 1 und Frei 40 für Gleis 2 und 3.
