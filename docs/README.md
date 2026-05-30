# Dokumentacja

Ten katalog zawiera dokumentację projektu City Audio Monitor: raporty PDF z kolejnych etapów pracy oraz schematy techniczne wygenerowane z PlantUML.

## Zawartość

```text
docs/
├── diagrams/                  # źródła PlantUML i wygenerowane SVG
├── Raport_Z2_2026_03_04.pdf
├── Raport_Z2_26_03_11.pdf
├── Raport_Z2_2026_03_17.pdf
├── Raport_Z2_2026_03_24.pdf
├── Raport_Z2_2026_03_31.pdf
├── Raport_Z2_2026_04_25.pdf
├── Raport_Z2_2026_05_05.pdf
└── Raport_Z2_2026_05_17.pdf
```

## Schematy

Aktualne schematy:

- `diagrams/01_architektura_systemu.svg` - ogólna architektura: embedded, aplikacja mobilna, backend, frontend i VPS.
- `diagrams/02_logika_urzadzenia_embedded.svg` - logika pracy urządzenia embedded.
- `diagrams/03_logika_backendu.svg` - logika pracy backendu.

Źródła tych diagramów są zapisane jako pliki `.puml` w tym samym katalogu.

## Regenerowanie schematów

Z katalogu głównego repozytorium:

```powershell
java -jar ..\plantuml-1.2026.4.jar -tsvg -charset UTF-8 .\docs\diagrams\*.puml
```

Pojedynczy diagram:

```powershell
java -jar ..\plantuml-1.2026.4.jar -tsvg -charset UTF-8 .\docs\diagrams\01_architektura_systemu.puml
```

## Zasady aktualizacji

- Raporty PDF są archiwum pracy projektowej i nie powinny być ręcznie edytowane w tym katalogu.
- Aktualne informacje techniczne powinny trafiać do README oraz diagramów.
- Po zmianie logiki backendu, firmware lub frontendu trzeba zaktualizować odpowiedni diagram `.puml` i ponownie wygenerować `.svg`.
- Diagramy powinny być opisane po polsku i odzwierciedlać aktualny przepływ danych w projekcie.
