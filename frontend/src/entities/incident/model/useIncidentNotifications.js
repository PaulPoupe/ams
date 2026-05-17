import { useEffect, useRef, useState } from 'react';
import { appConfig } from '@/app/config/env';

const createNotification = (incident) => ({
  id: `${incident.id}-${incident.createdAt}`,
  title: 'New triangulated point',
  message: incident.description,
  createdAt: incident.createdAt,
});

export function useIncidentNotifications(incidents, { isLoading = false } = {}) {
  const observedIncidentIdsRef = useRef(null);
  const [notifications, setNotifications] = useState([]);

  useEffect(() => {
    if (isLoading) {
      return;
    }

    if (observedIncidentIdsRef.current === null) {
      observedIncidentIdsRef.current = new Set(incidents.map((incident) => incident.id));
      return;
    }

    const newIncidents = incidents.filter(
      (incident) => !observedIncidentIdsRef.current.has(incident.id),
    );

    incidents.forEach((incident) => {
      observedIncidentIdsRef.current.add(incident.id);
    });

    if (!newIncidents.length) {
      return;
    }

    setNotifications((currentNotifications) =>
      [
        ...newIncidents.map(createNotification),
        ...currentNotifications,
      ].slice(0, appConfig.alerts.maxNotifications),
    );
  }, [incidents, isLoading]);

  useEffect(() => {
    if (!notifications.length) {
      return undefined;
    }

    const timeoutId = window.setTimeout(() => {
      setNotifications((currentNotifications) => currentNotifications.slice(0, -1));
    }, appConfig.alerts.dismissMs);

    return () => {
      window.clearTimeout(timeoutId);
    };
  }, [notifications]);

  const dismissNotification = (notificationId) => {
    setNotifications((currentNotifications) =>
      currentNotifications.filter((notification) => notification.id !== notificationId),
    );
  };

  return {
    dismissNotification,
    notifications,
  };
}
