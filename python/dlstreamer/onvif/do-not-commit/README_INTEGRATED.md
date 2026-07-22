# ONVIF Camera Manager UI - Integrated Version

Demo aplikacji graficznej zintegrowanej z pełnym ONVIF API suite.

## Cechy

✅ **Real ONVIF Discovery** - `discover_onvif_cameras()` znajduje kamery w sieci  
✅ **Media Profiles** - `read_camera_profiles()` pobiera profile i RTSP URLs  
✅ **Event Topics** - `get_supported_event_topics()` wyświetla obsługiwane zdarzenia  
✅ **Manual Camera Entry** - dodawanie kamery ręcznie (hostname:port)  
✅ **Credentials Support** - username/password dla dostępu do kamer  
✅ **Background Threading** - discovery/profil load bez zawieszenia UI  
✅ **4 Zakładki**:
  - **Cameras & Profiles** — lista kamer + media profile'i z RTSP
  - **Event Topics** — zdarzenia z `GetEventProperties` (topic, source fields, data fields)
  - **Pipelines & Bindings** — mock pipeline'i i statyczne powiązania

## Uruchomienie

### 1. Ze środeowiskiem wirtualnym (zalecane):
```bash
cd /home/labrat/intel/onvif-vippet-library/dlstreamer
source .venv/bin/activate
cd python/dlstreamer/onvif/do-not-commit
python demo_ui_integrated.py
```

### 2. Bez venv:
```bash
cd /home/labrat/intel/onvif-vippet-library/dlstreamer/python/dlstreamer/onvif/do-not-commit
python3 demo_ui_integrated.py
```

## Instrukcja użycia

### Odkrywanie kamer:
1. Wpisz **username** i **password** (domyślnie: `admin` / `r00tme`)
2. Kliknij przycisk **"🔍 Discover Cameras"**
3. Aplikacja automatycznie wyszuka wszystkie kamery ONVIF w sieci

### Ręczne dodanie kamery:
1. Kliknij **"Add Manual Camera"**
2. Wpisz hostname (lub IP) i port
3. Kliknij **Add**

### Przeglądanie kamer:
1. Kliknij na kamerę z listy po lewej
2. Prawa strona wyświetli:
   - **Detale kamery** (hostname, port)
   - **Media Profile'i** (nazwa, encoding, RTSP URI)

### Przeglądanie event topics:
1. Przejdź na zakładkę **"Event Topics"**
2. Po wybraniu kamery, zobaczysz wszystkie obsługiwane zdarzenia z pełnym schematem:
   - **Topic Path** — ścieżka zdarzenia (np. `tns1:RuleEngine/MotionDetector/Motion`)
   - **Kind** — event lub property
   - **Source Fields** — pola źródłowe z typami (np. `Rule:xsd:string`)
   - **Data Fields** — pola danych (np. `IsMotion:xsd:boolean`)

## Integracja z bibliotekami

Aplikacja importuje i używa:

```python
from dlstreamer.onvif.discovery import discover_onvif_cameras
from dlstreamer.onvif.camera_profiles import read_camera_profiles
from dlstreamer.onvif.event_manager import get_supported_event_topics
```

### API Endpoints

| Funkcja | Źródło | Cel |
|---------|--------|-----|
| `discover_onvif_cameras()` | `discovery` | WS-Discovery (port 3702) |
| `read_camera_profiles()` | `camera_profiles` | GetProfiles (Media service) |
| `get_supported_event_topics()` | `event_manager` | GetEventProperties (Events service) |

## Mock Data

Zakładka **"Pipelines & Bindings"** zawiera mock dane:
- **motion_detection_pipeline** ✓ (loaded)
- **person_detection_pipeline** ✓ (loaded)  
- **vehicle_tracking_pipeline** ✗ (not loaded)

W przyszłości można zintegrować z rzeczywistą bazą konfiguracji pipeline'ów.

## Troubleshooting

### "No cameras found"
- Upewnij się, że kamery ONVIF są w tej samej sieci
- Sprawdź firewall (WS-Discovery używa UDP port 3702)
- Spróbuj **"Add Manual Camera"** z konkretnym IP

### "Profile Error: Failed to read profiles"
- Sprawdź username/password
- Niektóre kamery wymagają specjalnych uprawnień
- Sprawdź czy kamera wspiera Media service

### "Event topics nie ładują"
- Kamera może nie wspierać Events service
- To jest silent fail — aplikacja kontynuuje

## Architektura

```
demo_ui_integrated.py
    │
    ├─→ discovery.discover_onvif_cameras()
    │       └─→ WS-Discovery probe → camera list
    │
    ├─→ camera_profiles.read_camera_profiles()
    │       └─→ GetProfiles (SOAP) → profiles + RTSP URLs
    │
    └─→ event_manager.get_supported_event_topics()
            └─→ GetEventProperties (SOAP) → event schema
```

## Przyszłe rozszerzenia

- [ ] Zapis/odczyt konfiguracji pipeline'ów
- [ ] Live event streaming (stream_events API)
- [ ] PTZ controls (ptz library)
- [ ] Nagrywanie i playback
- [ ] Export konfiguracji do YAML/JSON
