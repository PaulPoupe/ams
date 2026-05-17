from pydantic_settings import BaseSettings

class Settings(BaseSettings):
    PROJECT_NAME: str = "AMS Backend"
    API_V1_STR: str = "/api"
    DATABASE_URL: str

    # Audio Settings
    AUDIO_SAMPLE_RATE: int = 16000
    AUDIO_CHANNELS: int = 1
    AUDIO_BIT_DEPTH: int = 16
    AUDIO_STORAGE_PATH: str = "storage/audio"
    CORS_ALLOW_ORIGINS: str = (
        "http://localhost,"
        "http://127.0.0.1,"
        "http://localhost:80,"
        "http://127.0.0.1:80,"
        "http://localhost:8080,"
        "http://127.0.0.1:8080,"
        "http://localhost:5173,"
        "http://127.0.0.1:5173,"
        "http://localhost:5174,"
        "http://127.0.0.1:5174"
    )

    @property
    def cors_allow_origins(self) -> list[str]:
        return [origin.strip() for origin in self.CORS_ALLOW_ORIGINS.split(",") if origin.strip()]

    model_config = {
        "env_file": ".env",
        "case_sensitive": True,
        "extra": "ignore"
    }

settings = Settings()
