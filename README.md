# ESP32-signal

ESP32-Signal ist eine Signalsteuerung für die Eisenbahn. Sie bietet ein Blocksignalsystem auf Basis von Blockbausteinen, die mit einem ESP32 betrieben werden. Die Kommunikation zwischen den Bausteinen erfolgt über dsa ESP-NOW Protokoll. Die Kommunikation verläuft über ESP-NOW immer zwischen den benachbarten Blöcken. 

## Funktion

Die Bahnstrecke zwischen zwei Bahnhöfen ist in Blöcke geteilt. An den Blockgrenzen befinden sich die Blockbausteine, die zwei Signale für die beiden Fahrtrichtungen betreiben, sowie die vorbeifahrenden Achsen mit einem Achszähler erfassen.

Die Strecke kann für eine Fahrtrichtung beantragt werden. Das erfolt über eine Taste am ersten Blockbaustein durch den Fahrtdiensleiter. Die Fahrtrichtung wird festgelegt, wenn keine Zugfahrt für die Gegenrichtung beantragt wurde und die Strecke frei von Zügen ist.

Das Blocksignal an der Einfahrt in einen Block darf nur Frei anzeigen, wenn die Fahrtrichtung festgelegt ist und sich kein Zug im Block befindet. Der Zustand wird über die Achszähler ermittelt. Die gezählten Achsen bei der Einfahrt in den Block und bei der Ausfahrt = Einfahrt in den nächten Block müssen übereinstimmen. Dann gilt der Block als frei und das Signal darf Frei zeigen.

Ein Blocksignal geht nur in die Freistellung, wenn ein Zug in den davor liegende Block einfährt (Vorblock). Der Vorblock wird registriert und das Signal auf Frei gestellt, sobald es zulässig ist.

Fährt ein Zug in einen Block ein, fällt das Blocksignal sofort auf Halt. Der Block ist gesperrt. Hat der Zug den Block am Ende gänzlich verlassen, indem alle Achsen über den Achszähler gefahren sind, wird der Block wieder freigegeben. Das Signal bleibt solange auf Halt bis der nächsten Vorblock empfangen wird oder davor schon ein Vorblock registriert wurde.

## Signale

Die Signale können drei Begriffe zeigen:

- rotes Licht = Halt
- grünes Licht = Frei
- gelbes Licht = Frei mit Geschwindigkeitsbegrenzung

Blocksignale besitzten immer nur die Begriffe Halt und Frei. Ausfahrts- und Einfahrtssignale eines Bahnhofes können auch den dritten Begriff zeigen, wenn Weichen in die Ablenkung (in den Bogen) befahren werden.

## Achszähler

Die Achszähler bestehen aus zwei Kontakten, die in kurzem Abstand hintereinander am Gleis angeordnet sind. Rollt ein Rad vorbei, wird das Rad und die Fahrtrichtung erkannt. Der Zähler zählt entsprechend der Fahrtrichtung hinauf oder herunter. Wird vom nächsten Blockbaustein ein Rückblock erhalten, wird der Zähler auf Null gesetzt.

Jeweils zwei Achszähler arbeiten zusammen. Der Zähler am Beginn des Blocks und der Zähler für den nächsten Block müssen die gleiche Anzahl an Achsen zählen. Nur dann gilt der Block als frei von Fahrzeugen.

Der Zählvorgang in einem Blockbaustein beginnt bei Null, wenn der Block frei war und ein Zug einfährt. Es wird sofort der Vorblock gesendet. Der Baustein sendet die Achsanzahl zum nächsten Block.

Ein Rückblock wird vom nächsten Baustein gesendet, wenn dessen Achszähler die gleiche Achsanzahl gezählt hat. Der Zähler wird somit auf Null gestellt und der Block ist frei.

## Fahrtrichtung

An beiden Streckenenden gibt es eine Bedienung, um die Strecke (Fahrtrichtung) beantragen zu können. Eine mehrfarbige LED zeigt den Zustand an.

- LED gelb: Die Strecke ist frei und keine Fahrtrichtung ist beantragt.
- LED grün: (am Startbahnhof): Die Strecke ist festgelegt und kann befahren werden. An das erste Signal (Ausfahrtssignal) wird ein Vorblock geschickt.
- LED grün blinkend: (am Startbahnhof): Die Strecke wurde für die Gegenrichtung beantragt. Nachdem der letzte Zug die Strecke verlassen hat, wird die Fahrtrichtung umgekehrt.

- LED rot: (am Zielbahnhof): Die Strecke ist in die Gegenrichtung festgelegt.
- LED rot blinkend: (am Zielbahnhof): Die gesperrte Strecke wurde beantragt. Die Fahrtrichtung wird umgedreht und festgelegt, sobald der letzte Zug die Strecke verlassen hat. 

Haben alle Züge die Strecke verlassen, fällt die Strekce in den LED gelb-Modus.

Mit einer Löschtaste kann vom Startbahnhof die eigene Fahrtrichtung wieder freigegeben werden, wenn sich auf der Strecke kein Zug befindet.

## Weichen

In die Ausfahrt und Einfahrt der Bahnhöfe können bis zu vier Weichen integriert werden. In dem Fall gibt es eine Gleistaste für jedes Gleis und eine Streckentaste. Eine Ausfahrt wird durch gleichzeitiges Drücken der Gleistaste und der Streckentaste gestellt, eine Einfahrt durch die Einfahrtsignaltaste und die Gleistaste. Die Fahrtrichtung wird dementsprechend beantragt.

Die Zuortnung der Weichenstellungen zu den Fahrstraßen erfolgt über ein Webservice, dass auf den ESP32_Relais_X8 Bausteinen läuft.

## Hardware

Die Blockbausteine laufen auf einer ESP32_Relay_X2 Platine mit zwei Relais für die Signale der beiden Fahrtrichtungen.

Die Ausfahrts- und Einfahrtssignale laufen auf einer ESP32_Relais_X8 Platine. Vier Relais sind jeweils für die Signale (Halt - Frei/Frei-langsam) bestimmt, die vier weiteren Relais können jeweils eine Weiche stellen.
