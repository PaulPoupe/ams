import { EyeOff } from 'lucide-react';
import { Popup } from 'react-map-gl';
import { formatCoordinate, formatPreciseDateTime } from '@/shared/lib/format';

export function IncidentPopup({ incident, onClose, onHide }) {
  if (!incident) {
    return null;
  }

  return (
    <Popup
      anchor="top"
      className="incident-map-popup"
      closeOnClick={false}
      latitude={incident.lat}
      longitude={incident.lng}
      maxWidth="320px"
      onClose={onClose}
    >
      <article className="incident-popup">
        <div className="incident-popup__header">
          <span>#{incident.id}</span>
          <strong>{incident.classification}</strong>
        </div>

        <p>{incident.description}</p>

        <dl className="incident-popup__details">
          <div>
            <dt>Detected</dt>
            <dd>{formatPreciseDateTime(incident.createdAt)}</dd>
          </div>
          <div>
            <dt>Coordinates</dt>
            <dd>
              {formatCoordinate(incident.lat)}, {formatCoordinate(incident.lng)}
            </dd>
          </div>
        </dl>

        <div className="incident-popup__actions">
          <button
            type="button"
            className="button-secondary incident-popup__hide"
            onClick={() => onHide?.(incident)}
          >
            <EyeOff size={15} />
            <span>Hide</span>
          </button>
        </div>
      </article>
    </Popup>
  );
}
