# Plan implementacji aplikacji Android dla AMS

## Cel

Aplikacja Android powinna działać jako mobilny czujnik systemu AMS, a nie jako kopia webowego frontendu. Frontend webowy pozostaje odpowiedzialny za mapę, panel administracyjny i podgląd incydentów. Aplikacja mobilna ma zachowywać się jak dodatkowe urządzenie pomiarowe.

Główne zadania aplikacji:

- zarejestrowanie telefonu jako urządzenia w backendzie,
- synchronizacja czasu z serwerem,
- wysyłanie raportów stanu urządzenia,
- wykrywanie zdarzenia akustycznego,
- wysłanie `SoundEvent` oraz krótkiego fragmentu audio do backendu.

## Proponowana struktura

```text
ams/
├── backend/
├── frontend/
├── embeded/
├── mobile/                 # nowa aplikacja Android
│   ├── build.gradle.kts
│   └── src/main/...
├── settings.gradle.kts
├── build.gradle.kts
├── gradle/
├── gradlew
└── gradlew.bat
```

Najlepiej dodać nowy moduł `mobile` i wykorzystać istniejący Gradle wrapper z katalogu głównego repozytorium.

## Technologie

- Kotlin,
- Jetpack Compose,
- Material 3,
- Retrofit + OkHttp,
- kotlinx.serialization albo Moshi,
- DataStore do zapisu ustawień,
- WorkManager do cyklicznych raportów health,
- Foreground Service do nagrywania mikrofonu,
- AudioRecord do detekcji zdarzeń dźwiękowych.

## Etapy realizacji

### 1. Utworzenie aplikacji Android

- utworzyć moduł `mobile`,
- ustawić package, np. `com.github.paulpoupe.ams.mobile`,
- dodać uprawnienia:
  - `INTERNET`,
  - `RECORD_AUDIO`,
  - `ACCESS_FINE_LOCATION`,
  - `POST_NOTIFICATIONS`,
  - uprawnienia dla foreground service,
- przygotować prosty szkielet UI w Jetpack Compose.

### 2. Warstwa API

Należy przygotować klienta HTTP dla obecnego backendu AMS.

Obsługiwane endpointy:

- `GET /api/health`,
- `POST /api/devices`,
- `GET /api/devices`,
- `GET /api/devices/{device_id}/time/sync`,
- `POST /api/devices/{device_id}/health`,
- `POST /api/devices/{device_id}/sound-events`,
- `POST /api/devices/{device_id}/sound-events/{event_id}/audio`.

Warstwę API warto wydzielić do osobnego pakietu, np.:

```text
mobile/src/main/java/.../data/api/
mobile/src/main/java/.../data/model/
```

### 3. Rejestracja telefonu jako urządzenia

Aplikacja powinna:

- wygenerować stały `device_id`,
- zapisać go w DataStore,
- zarejestrować urządzenie w backendzie,
- ustawić nazwę, np. `Phone sensor`,
- ustawić `tag = mobile`,
- pobrać współrzędne GPS.

Jeśli GPS nie jest dostępny, aplikacja powinna pozwolić na ręczne wpisanie `lat` i `lon`, ponieważ backend wymaga lokalizacji urządzenia.

### 4. Minimalny interfejs MVP

Pierwsza wersja aplikacji powinna zawierać proste ekrany:

- konfiguracja adresu backendu,
- sprawdzenie połączenia z `/api/health`,
- rejestracja albo wybór urządzenia,
- status synchronizacji czasu,
- przełącznik wysyłania health reports,
- przycisk wysłania testowego zdarzenia,
- prosty log ostatnich operacji.

Na tym etapie nie trzeba jeszcze implementować mapy ani pełnego widoku operatora.

### 5. Testowe zdarzenie dźwiękowe

Pierwszy działający przepływ powinien być prosty:

- użytkownik naciska przycisk `Send test event`,
- aplikacja tworzy `SoundEvent`,
- aplikacja wysyła krótki plik audio albo bufor testowy,
- backend zapisuje zdarzenie,
- frontend webowy może pokazać wynik na mapie.

Na tym etapie można użyć statycznego pliku `.wav` albo `.pcm16` z `res/raw`.

### 6. Nagrywanie mikrofonu i detekcja

Po sprawdzeniu działania API można dodać rzeczywistą detekcję:

- użyć `AudioRecord`,
- format audio: `16000 Hz`, mono, `PCM16`,
- utrzymywać bufor pierścieniowy około 10 sekund,
- liczyć `peak_level`, `rms_level` i `noise_floor`,
- wykrywać impuls prostym progiem,
- po wykryciu wysłać metadane zdarzenia,
- po utworzeniu zdarzenia wysłać fragment audio.

Nie należy na początku dodawać rozbudowanego ML ani ciągłego streamingu. Obecna architektura AMS jest event-first, więc aplikacja powinna wysyłać tylko istotne zdarzenia.

### 7. Synchronizacja czasu

Synchronizacja czasu jest ważna dla triangulacji TDOA. Minimalnie można zacząć od:

```text
System.currentTimeMillis() * 1_000_000
```

Docelowo aplikacja powinna mieć osobny `ClockSyncRepository`, który:

- cyklicznie wywołuje endpoint synchronizacji czasu,
- mierzy opóźnienie round-trip,
- zapisuje offset względem czasu serwera,
- używa skorygowanego czasu przy tworzeniu `event_time_ns`.

### 8. Raporty health

Raporty health powinny działać cyklicznie przez WorkManager.

Przykładowe dane:

- wersja aplikacji,
- uptime aplikacji,
- status sieci,
- aktywność mikrofonu,
- poziom baterii,
- status ładowania,
- liczba odrzuconych buforów audio,
- krótki komunikat statusu.

Dane należy zmapować na obecny model `DeviceHealthReport` w backendzie.

### 9. Testy i weryfikacja

Podstawowy scenariusz testowy:

1. Uruchomić backend przez `docker compose up --build`.
2. Na emulatorze używać adresu `http://10.0.2.2`.
3. Na fizycznym telefonie używać adresu LAN albo VPS.
4. Sprawdzić `/api/health`.
5. Zarejestrować urządzenie mobilne.
6. Wykonać synchronizację czasu.
7. Wysłać health report.
8. Wysłać testowy `SoundEvent`.
9. Wysłać audio dla zdarzenia.
10. Sprawdzić, czy frontend webowy widzi zdarzenia i incydenty.

Dodatkowo warto sprawdzić scenariusz z trzema urządzeniami, ponieważ backend tworzy incydent TDOA dopiero wtedy, gdy ma wystarczająco dużo zdarzeń z różnych źródeł.

### 10. Dokumentacja

Po dodaniu aplikacji należy zaktualizować:

- główny `README.md`,
- dodać `mobile/README.md`,
- zaktualizować diagram PlantUML architektury,
- opisać konfigurację backend URL dla emulatora, telefonu lokalnego i VPS.

## Kolejność prac

Najbezpieczniejsza kolejność:

1. Dodać moduł `mobile`.
2. Zrobić prosty Compose UI.
3. Dodać klienta API.
4. Dodać rejestrację urządzenia.
5. Dodać time sync.
6. Dodać health reports.
7. Dodać ręczne testowe `SoundEvent`.
8. Dodać upload testowego audio.
9. Dopiero potem dodać nagrywanie mikrofonu.
10. Na końcu dodać automatyczną detekcję impulsu i foreground service.

Taki podział pozwala szybko sprawdzić integrację z backendem i nie miesza problemów Androida, mikrofonu oraz triangulacji w jednym kroku.
