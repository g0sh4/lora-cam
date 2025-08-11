import serial
import io
from PIL import Image
from datetime import datetime

# --- KONFIGURACJA ---
# Zmień na właściwy port COM dla Twojego NODE_C
SERIAL_PORT = 'COM11' 
BAUD_RATE = 115200
# --------------------

def receive_and_save_image():
    """
    Główna funkcja do nasłuchiwania na porcie szeregowym,
    odbierania i zapisywania obrazu z NODE_C.
    """
    print(f"Nasluchuje na porcie {SERIAL_PORT} z predkoscia {BAUD_RATE}...")

    try:
        with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=5) as ser:
            while True:
                line = ser.readline()
                if line.strip() == b'START_IMAGE_TRANSFER':
                    print("\nWykryto poczatek transferu obrazu z NODE_C!")
                    size_line = ser.readline()
                    try:
                        image_size = int(size_line.strip())
                        print(f"Oczekiwany rozmiar obrazu: {image_size} bajtow.")
                    except (ValueError, TypeError):
                        print(f"Blad: Nieprawidlowy format rozmiaru obrazu: {size_line}")
                        continue

                    image_data = ser.read(image_size)

                    if len(image_data) != image_size:
                        print(f"Blad: Odebrano {len(image_data)}/{image_size} bajtow. Transfer niekompletny.")
                        continue

                    print("Obraz odebrany pomyslnie. Probuje zapisac...")
                    try:
                        image_stream = io.BytesIO(image_data)
                        img = Image.open(image_stream)
                        timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
                        filename = f"lora_image_{timestamp}.jpg"
                        img.save(filename)
                        print(f"Zapisano obraz jako: {filename}")
                    except Exception as e:
                        print(f"Blad podczas przetwarzania lub zapisywania obrazu: {e}")
                    
                    ser.reset_input_buffer()

    except serial.SerialException as e:
        print(f"BLAD KRYTYCZNY: Nie mozna otworzyc portu {SERIAL_PORT}.")
        print(f"   Upewnij sie, ze jestes podlaczony do NODE_C.")
        print(f"   Szczegoly bledu: {e}")
    except KeyboardInterrupt:
        print("\nZamykanie programu.")

if __name__ == '__main__':
    receive_and_save_image()