import requests
import time
import os
from datetime import datetime

# KONFIGURACJA
IP_ADRES = "192.168.1.43"  # Adres Twojej kamerki
URL = f"http://{IP_ADRES}/capture"
FOLDER_ZAPISU = "moje_klatki_dataset"
INTERVAL_MINUT = 1

if not os.path.exists(FOLDER_ZAPISU):
    os.makedirs(FOLDER_ZAPISU)

print(f"--- Start pobierania co {INTERVAL_MINUT} minut z {URL} ---")

try:
    while True:
        # Generowanie nazwy pliku z datą i godziną
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = os.path.join(FOLDER_ZAPISU, f"img_{timestamp}.jpg")

        try:
            # Pobranie zdjęcia
            response = requests.get(URL, timeout=10)
            if response.status_code == 200:
                with open(filename, 'wb') as f:
                    f.write(response.content)
                print(f"[ZAPISANO] {filename}")
            else:
                print(f"[BŁĄD] Kod statusu: {response.status_code}")
        except Exception as e:
            print(f"[BŁĄD POŁĄCZENIA] {e}")

        # Czekanie (5 minut * 60 sekund)
        time.sleep(INTERVAL_MINUT * 60)

except KeyboardInterrupt:
    print("\nZatrzymano skrypt.")