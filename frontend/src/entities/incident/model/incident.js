const readCoordinate = (value) => {
  if (value === null || value === undefined || value === '') {
    return null;
  }

  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
};

const readCreatedAt = (rawIncident) => {
  if (rawIncident.created_at) {
    return rawIncident.created_at;
  }

  const createdAtNs = rawIncident.created_at_ns ?? rawIncident.createdAtNs;
  if (createdAtNs !== null && createdAtNs !== undefined) {
    try {
      return new Date(Number(BigInt(createdAtNs) / 1_000_000n)).toISOString();
    } catch {
      const parsed = Number(createdAtNs);

      if (Number.isFinite(parsed)) {
        return new Date(parsed / 1_000_000).toISOString();
      }
    }
  }

  return new Date().toISOString();
};

export function mapIncident(rawIncident) {
  const lat = readCoordinate(rawIncident.lat);
  const lng = readCoordinate(rawIncident.lon ?? rawIncident.lng);

  return {
    id: String(rawIncident.id),
    lat,
    lng,
    description: rawIncident.description?.trim() || 'No description provided.',
    createdAt: readCreatedAt(rawIncident),
    classification: 'Suspicious incident',
    source: 'backend',
  };
}

export function isIncidentLocated(incident) {
  return Number.isFinite(incident.lat) && Number.isFinite(incident.lng);
}
