# Temat 18: Hala widowiskowo-sportowa

## 1. Opis projektu

Celem projektu jest symulacja zarządzania Halą Widowiskowo-Sportową podczas meczu siatkówki przy użyciu mechanizmów programowania współbieżnego (IPC). System odwzorowuje zachowanie kibiców, personelu oraz logistykę imprezy masowej o pojemności **K**.

### 1.1. Kluczowe parametry
* **Pojemność (K):** Całkowita liczba kibiców.
* **Sektory:** 8 sektorów zwykłych (pojemność K/8 każdy) + 1 sektor VIP.
* **Kasy biletowe:** Łącznie 10 stanowisk.
* **Wejścia:** Osobne wejście do każdego z 8 sektorów + osobne wejście VIP.
* **Procesy:**
    * **Kierownik:** Nadzoruje wydarzenie i wydaje sygnały sterujące.
    * **Kasjer:** Obsługuje sprzedaż biletów.
    * **Pracownik techniczny:** Steruje przepływem osób przy wejściach do sektorów.
    * **Kibic:** Symuluje cykl życia widza (kolejka, kupno biletu, kontrola, mecz).

### 1.2. Logika systemu i zasady

#### A. Sprzedaż biletów (Kasy)
* **Dynamiczne działanie:**
    * Zawsze czynne są min. 2 kasy.
    * **Zasada otwierania:** Na każde `K/10` kibiców w kolejce musi przypadać min. 1 czynna kasa.
    * **Zasada zamykania:** Jeśli liczba kibiców w kolejce jest mniejsza niż `(K/10) * (N-1)` (gdzie N to liczba czynnych kas), jedna kasa jest zamykana.
    * Kasy zamykają się automatycznie po wyprzedaniu wszystkich biletów.
* **Zasady zakupu:**
    * Jeden kibic może kupić maks. 2 bilety w tym samym sektorze.
    * Bilety są sprzedawane losowo do poszczególnych sektorów przez wszystkie kasy.
    * VIP (< 0,3% * K) omijają kolejkę, ale muszą kupić bilet.

#### B. Kontrola bezpieczeństwa i wejście
* **VIP:** Wchodzą/wychodzą osobnym wejściem bez kontroli bezpieczeństwa.
* **Kibice zwykli:**
    * Przechodzą kontrolę przy wejściu do swojego sektora.
    * **Równoległość:** Każde wejście ma 2 równoległe stanowiska kontroli.
    * **Pojemność:** Na jednym stanowisku mogą przebywać jednocześnie maks. 3 osoby.
    * **Ograniczenie:** Jeśli na stanowisku jest >1 osoba, muszą to być kibice tej samej drużyny.
    * **Cierpliwość:** Kibic może przepuścić w kolejce maks. 5 innych osób. Przekroczenie tej liczby wywołuje agresję (stan niedopuszczalny).
    * **Dzieci:** Osoby poniżej 15 r.ż. wchodzą pod opieką dorosłego.

#### C. Zarządzanie zdarzeniami (Kierownik i Pracownicy Techniczni)
Kierownik wydaje polecenia (sygnały) obsługiwane przez pracowników technicznych:
1.  **Sygnał 1 (Wstrzymanie):** Pracownik wstrzymuje wpuszczanie kibiców do sektora.
2.  **Sygnał 2 (Wznowienie):** Pracownik wznawia wpuszczanie kibiców.
3.  **Sygnał 3 (Ewakuacja/Koniec):** Wszyscy kibice opuszczają stadion. Pracownik raportuje do kierownika, gdy sektor jest pusty.

## 2. Zakres realizacji

* **Procesy/Wątki:** Do reprezentacji aktorów (Kierownik, Kasjerzy, Kibice, Pracownicy techniczni).
* **Semafory:** Do synchronizacji (np. dostęp do licznika biletów, limity na stanowiskach kontroli).
* **Pamięć współdzielona/Kolejki komunikatów:** Do komunikacji między Kierownikiem a pracownikami oraz współdzielenia stanu (liczniki, rozmiary kolejek).

## 3. Scenariusze testowe

Raport z symulacji będzie obejmował następujące testy weryfikujące poprawność działania:

1.  **Test skalowalności kas:**
    * Symulacja nagłego napływu kibiców, aby sprawdzić, czy otwierają się dodatkowe kasy.

2.  **Test skalowalności kas:**
    * Symulacja spadku liczby kibiców w kolejce, aby sprawdzić, czy kasy się zamykają.

3.  **Test agresji:**
    * Scenariusz, w którym wolna kolejka zmusza kibica do przepuszczenia >5 osób. Weryfikacja, czy program poprawnie obsługuje stan agresji.

4.  **Test sygnałów Kierownika:**
    * **Stop/Wznowienie:** Weryfikacja, że kibice nie wchodzą do sektora po Sygnale 1 i mogą wejść po Sygnale 2.
    * **Ewakuacja:** Potwierdzenie, że po Sygnale 3 wszyscy opuszczają obiekt, a sektory raportują stan "pusty".

5.  **Test kontroli na bramkach:**
    * Weryfikacja, czy osoby wnoszące race zostaną wyrzucone.

## 4. Link do repozytorium
https://github.com/Filip-pk/Hala-widowiskowo-sportowa