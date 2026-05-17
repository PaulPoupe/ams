import { Map, NavigationControl } from 'react-map-gl';
import { appConfig, isMapConfigured } from '@/app/config/env';
import { IncidentMarker } from '@/entities/incident/ui/IncidentMarker';
import { IncidentPopup } from '@/entities/incident/ui/IncidentPopup';
import { SensorMarker } from '@/entities/sensor/ui/SensorMarker';

export function OperationsMap({
  incidents,
  onClearSelection,
  onHideIncident,
  onSelectIncident,
  onSelectSensor,
  onViewStateChange,
  selectedIncident,
  selectedSensor,
  sensors,
  viewState,
}) {
  if (!isMapConfigured) {
    return (
      <div className="map-placeholder">
        <div className="map-placeholder__content">
          <span className="map-placeholder__eyebrow">Map disabled</span>
          <h2>Set <code>VITE_MAPBOX_TOKEN</code> to enable the operator map.</h2>
          <p>
            The rest of the console works without the basemap.
          </p>
        </div>
      </div>
    );
  }

  return (
    <Map
      {...viewState}
      mapStyle={appConfig.mapStyle}
      mapboxAccessToken={appConfig.mapboxToken}
      reuseMaps
      style={{ width: '100%', height: '100%' }}
      onClick={onClearSelection}
      onMove={(event) => onViewStateChange(event.viewState)}
    >
      <NavigationControl position="bottom-right" />

      {sensors.map((sensor) => (
        <SensorMarker
          key={sensor.id}
          sensor={sensor}
          isActive={selectedSensor?.id === sensor.id}
          onSelect={onSelectSensor}
        />
      ))}

      {incidents.map((incident) => (
        <IncidentMarker
          key={incident.id}
          incident={incident}
          isSelected={selectedIncident?.id === incident.id}
          onSelect={onSelectIncident}
        />
      ))}

      <IncidentPopup incident={selectedIncident} onClose={onClearSelection} onHide={onHideIncident} />
    </Map>
  );
}
