import { Mic } from 'lucide-react';
import { Marker } from 'react-map-gl';
import { cx } from '@/shared/lib/cx';
import { getSensorTone } from '@/entities/sensor/model/sensor';

export function SensorMarker({ sensor, isActive, onSelect }) {
  const tone = isActive ? 'alert' : getSensorTone(sensor);

  const handleClick = (event) => {
    event.stopPropagation();
    onSelect?.(sensor);
  };

  return (
    <Marker latitude={sensor.lat} longitude={sensor.lng}>
      <button
        type="button"
        className={cx(
          'map-marker',
          'map-marker--sensor',
          `map-marker--${tone}`,
          isActive && 'is-selected',
        )}
        onClick={handleClick}
        title={sensor.name}
        aria-label={`Open ${sensor.name} details`}
      >
        <Mic size={16} strokeWidth={2.3} />
        <span className="map-marker__halo" />
      </button>
    </Marker>
  );
}
