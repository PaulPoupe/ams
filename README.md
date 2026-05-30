# City Audio Monitor

City Audio Monitor to system IoT do wykrywania podejrzanych zdarzeń akustycznych w przestrzeni miejskiej. Projekt składa się z urządzeń embedded z mikrofonami, backendu API, bazy danych PostGIS oraz aplikacji webowej dla operatora. Docelowy wariant uruchomieniowy zakłada działanie usług na VPS w kontenerach Docker, za bramką Nginx.

## Główna idea

Urzadzenia pomiarowe nasłuchują otoczenia lokalnie i wysyłają do serwera tylko istotne zdarzenia, zamiast przesyłać ciągły strumień audio. Backend zapisuje zdarzenia, status urządzeń i informacje o synchronizacji czasu. Gdy kilka urządzeń zarejestruje ten sam impuls w krótkim oknie czasowym, backend może wyznaczyć przybliżoną lokalizację źródła dźwięku metodą TDOA.

Frontend pokazuje urządzenia i incydenty na mapie oraz udostępnia panel administracyjny do obsługi czujników i danych systemowych.

## Struktura repozytorium

```text
ams/
├── backend/    # FastAPI, SQLAlchemy, PostGIS, logika zdarzeń i triangulacji
├── frontend/   # React + Vite, mapa operatora i panel administracyjny
├── embeded/    # firmware Raspberry Pi Pico 2 W
├── docs/       # raporty PDF i schematy PlantUML/SVG
├── docker-compose.yml
└── nginx.conf
```

Katalog `presentation/` zawiera materiały pomocnicze i nie jest główną częścią aktualnej architektury systemu.

## Architektura

Najważniejsze elementy:

- `embeded` - urządzenie Pico 2 W z mikrofonem I2S, wyświetlaczem e-paper, pomiarem zasilania INA219 i buforem audio.
- `backend` - API FastAPI obsługujące urządzenia, health reports, synchronizację czasu, zdarzenia dźwiękowe i incydenty.
- `db` - PostgreSQL z rozszerzeniem PostGIS do przechowywania lokalizacji urządzeń i incydentów.
- `frontend` - aplikacja webowa z mapą, markerami czujników, powiadomieniami o incydentach i panelem admina.
- `gateway` - Nginx kierujący ruch do backendu i frontendu.

Schematy architektury znajdują się w [docs/diagrams](docs/diagrams).

## Szybkie uruchomienie

Wymagania:

- Docker i Docker Compose
- wolny port `80`
- token Mapbox dla pełnego działania mapy

Uruchomienie całego stacka:

```powershell
docker compose up --build
```

Po starcie:

- aplikacja webowa: `http://localhost/`
- panel admina: `http://localhost/admin/`
- healthcheck backendu: `http://localhost/api/health`
- dokumentacja OpenAPI: `http://localhost/api/docs`

Domyślna baza danych jest tworzona w kontenerze `postgis/postgis:15-3.3`. Backend tworzy brakujące tabele przy starcie, bez automatycznego usuwania istniejących danych.

## Konfiguracja

Najważniejsze zmienne:

```text
POSTGRES_USER
POSTGRES_PASSWORD
POSTGRES_DB
DATABASE_URL
HTTP_PUBLISHED_PORT
VITE_MAPBOX_TOKEN
VITE_API_BASE_URL
VITE_MAP_CENTER_LAT
VITE_MAP_CENTER_LON
VITE_MAP_ZOOM
```

W trybie Docker większość ustawień bazy jest ustawiana w `docker-compose.yml`. Dla lokalnej pracy backendu poza Dockerem trzeba podać `DATABASE_URL`.

## Główne API

Backend działa pod prefiksem `/api`.

- `GET /api/health` - sprawdzenie działania backendu.
- `/api/devices` - rejestracja, lista, edycja i usuwanie urządzeń.
- `/api/devices/{device_id}/time/sync` - synchronizacja czasu urządzenia.
- `/api/devices/{device_id}/health` - raporty stanu urządzenia.
- `/api/devices/{device_id}/sound-events` - zdarzenia dźwiękowe wykryte lokalnie.
- `/api/devices/{device_id}/sound-events/{event_id}/audio` - upload fragmentu audio dla zdarzenia.
- `/api/suspicious_incidents` - lista i obsługa incydentów.

Szczegóły pól request/response są dostępne w Swagger UI pod `/api/docs`.

## Dokumentacja

Dokumentacja projektowa znajduje się w [docs](docs):

- raporty PDF z kolejnych etapów pracy,
- schemat ogólnej architektury,
- schemat logiki urządzenia embedded,
- schemat logiki backendu,
- pliki źródłowe PlantUML.

## Praca nad komponentami

Dokumentacja poszczególnych części:

- [backend/README.md](backend/README.md)
- [frontend/README.md](frontend/README.md)
- [embeded/README.md](embeded/README.md)
- [docs/README.md](docs/README.md)

## Uwagi projektowe

- System preferuje przepływ event-first: urządzenie wysyła zdarzenie i opcjonalny fragment audio, a nie stały strumień.
- Synchronizacja czasu jest istotna dla triangulacji TDOA.
- VPS jest naturalnym miejscem wdrożenia, ponieważ frontend, backend, baza i gateway mogą działać jako jeden stack Docker Compose.
- Aplikacja mobilna może korzystać z tego samego modelu API co urządzenia embedded.
