import serial
import io
from PIL import Image
from datetime import datetime

# --- KONFIGURACJA ---
# Zmień na właściwy port COM dla Twojego ESP32 (np. 'COM3' w Windows,
# '/dev/tty.usbserial-XXXX' na macOS lub '/dev/ttyUSB0' na Linux)
SERIAL_PORT = 'COM11'
BAUD_RATE = 115200
# --------------------

def receive_and_display_image():
    """
    Główna funkcja do nasłuchiwania na porcie szeregowym,
    odbierania i wyświetlania obrazu.
    """
    print(f"Nasluchuje na porcie {SERIAL_PORT} z predkoscia {BAUD_RATE}...")

    try:
        # Otwórz port szeregowy
        with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=5) as ser:
            while True:
                # Czekaj na znacznik rozpoczęcia transferu
                line = ser.readline()
                if line.strip() == b'START_IMAGE_TRANSFER':
                    print("\nWykryto poczatek transferu obrazu!")

                    # Następna linia to rozmiar obrazu
                    size_line = ser.readline()
                    try:
                        image_size = int(size_line.strip())
                        print(f"Oczekiwany rozmiar obrazu: {image_size} bajtow.")
                    except (ValueError, TypeError):
                        print(f"Blad: Nieprawidlowy format rozmiaru obrazu: {size_line}")
                        continue

                    # Odczytaj dokładnie tyle bajtów, ile wynosi rozmiar obrazu
                    image_data = ser.read(image_size)

                    if len(image_data) != image_size:
                        print(f"Blad: Odebrano {len(image_data)}/{image_size} bajtow. Transfer niekompletny.")
                        continue

                    print("Obraz odebrany pomyslnie. Probuje wyswietlic...")

                    try:
                        # Użyj Pillow do otwarcia obrazu z danych w pamięci
                        image_stream = io.BytesIO(image_data)
                        img = Image.open(image_stream)

                        timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
                        filename = f"received_image_{timestamp}.jpg"
                        
                        # Zapisz obraz do pliku
                        img.save(filename)
                        print(f"Zapisano obraz jako: {filename}")
                    except Exception as e:
                        print(f"Blad podczas przetwarzania obrazu: {e}")

    except serial.SerialException as e:
        print(f"BLAD KRYTYCZNY: Nie mozna otworzyc portu {SERIAL_PORT}.")
        print(f"   Sprawdz, czy:")
        print(f"   1. ESP32 jest podlaczony.")
        print(f"   2. Port '{SERIAL_PORT}' jest poprawny.")
        print(f"   3. Zaden inny program (np. Arduino IDE Serial Monitor) nie blokuje portu.")
        print(f"   Szczegoly bledu: {e}")
    except KeyboardInterrupt:
        print("\nZamykanie programu.")

if __name__ == '__main__':
    receive_and_display_image()