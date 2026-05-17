import { startTransition, useEffect, useState } from 'react';
import { listSensors } from '@/entities/sensor/api/sensorApi';

const initialState = {
  sensors: [],
  error: null,
  isLoading: true,
  source: 'remote',
};

export function useSensors() {
  const [state, setState] = useState(initialState);

  useEffect(() => {
    let disposed = false;

    async function loadSensors() {
      try {
        const sensors = await listSensors();

        if (disposed) {
          return;
        }

        startTransition(() => {
          setState({
            sensors,
            error: null,
            isLoading: false,
            source: 'remote',
          });
        });
      } catch (error) {
        if (disposed) {
          return;
        }

        startTransition(() => {
          setState({
            sensors: [],
            error,
            isLoading: false,
            source: 'remote',
          });
        });
      }
    }

    loadSensors();

    return () => {
      disposed = true;
    };
  }, []);

  return state;
}
