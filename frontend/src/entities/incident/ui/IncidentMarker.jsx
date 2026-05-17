import { AlertCircle } from 'lucide-react';
import { Marker } from 'react-map-gl';
import { cx } from '@/shared/lib/cx';

export function IncidentMarker({
  incident,
  isSelected = false,
  onSelect,
}) {
  return (
    <Marker latitude={incident.lat} longitude={incident.lng}>
      <div className="incident-anchor">
        <button
          type="button"
          className={cx(
            'map-marker',
            'map-marker--incident-feed',
            isSelected && 'is-selected',
          )}
          onClick={(event) => {
            event.stopPropagation();
            onSelect?.(incident);
          }}
          title={incident.description}
        >
          <AlertCircle size={16} strokeWidth={2.2} />
          <span className="map-marker__radar" />
        </button>
      </div>
    </Marker>
  );
}
