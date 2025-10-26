import serial
import time
import os
import io
# Usunięto import Image i numpy, bo nie przetwarzamy obrazu
# from PIL import Image
# import numpy as np

# --- Konfiguracja ---
SERIAL_PORT = 'COM11' # Zmień na swój port
BAUD_RATE = 115200
START_MARKER = 'START_IMAGE_TRANSFER'
RESULTS_DIR = "wyniki_testow"
os.makedirs(RESULTS_DIR, exist_ok=True)
# --------------------

def log(message):
    """Proste logowanie na konsolę."""
    timestamp = time.strftime("%H:%M:%S")
    print(f"[{timestamp}] {message}")

def main():
    log(f"Uproszczony odbiornik obrazów. Czekam na {SERIAL_PORT}...")

    try:
        with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2) as ser:
            log("Połączono! Oczekiwanie na dane...")
            image_counter = 0 # Licznik zapisanych obrazów

            while True:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if not line: continue

                    # --- Logika odbioru ---
                    if line == START_MARKER:
                        log(f"\n[+] Wykryto marker startu!")
                        image_counter += 1

                        # Odczytaj linię z metadanymi
                        meta_line = ser.readline().decode('utf-8').strip()
                        log(f"[Node C Meta]: {meta_line}")
                        parts = meta_line.split(',')

                        if len(parts) != 4 or not all(p.strip().isdigit() for p in parts):
                            log(f"[!] Błąd: Oczekiwano 'size,width,height,block_size', otrzymano: '{meta_line}'")
                            continue

                        expected_image_size = int(parts[0].strip())
                        image_width = int(parts[1].strip())
                        image_height = int(parts[2].strip())
                        block_size = int(parts[3].strip())
                        log(f"[*] Oczekiwane Metadata: Rozmiar={expected_image_size}, W={image_width}, H={image_height}, Blok={block_size}.")

                        # Odczytaj dane obrazu
                        log(f"[*] Odbieranie {expected_image_size} bajtów obrazu...")
                        image_data = ser.read(expected_image_size)

                        if len(image_data) != expected_image_size:
                            log(f"[!] Błąd: Odebrano {len(image_data)}/{expected_image_size} bajtów. Transfer niekompletny.")
                            continue
                        log("[+] Dane obrazu odebrane poprawnie.")

                        image_timestamp = time.strftime("%Y%m%d-%H%M%S")
                        # Prosta nazwa pliku
                        filename = f"received_image_{image_counter:03d}_{image_timestamp}.jpg"
                        filepath = os.path.join(RESULTS_DIR, filename)

                        # --- Zapisywanie pliku ---
                        try:
                            with open(filepath, 'wb') as f:
                                f.write(image_data)
                            log(f"[*] Obraz zapisany jako: {filepath}")
                        except Exception as e:
                            log(f"[!] Błąd podczas zapisywania pliku: {e}")

                        log("\nOczekiwanie na kolejny obraz...")

                    elif line: # Loguj inne linie z Node C
                        log(f"[Node C]: {line}")

                except UnicodeDecodeError:
                     pass # Ignoruj błędy dekodowania
                except serial.SerialTimeoutException:
                     log("[Timeout] Brak danych w oknie czasowym.")
                     pass # Kontynuuj czekanie
                except Exception as e:
                    log(f"[!] Błąd w pętli głównej: {e}")
                    time.sleep(1)

    except serial.SerialException as e:
        log(f"BŁĄD KRYTYCZNY: Nie można otworzyć portu {SERIAL_PORT}. {e}")
    except KeyboardInterrupt:
        log("\nProgram zatrzymany przez użytkownika.")

if __name__ == '__main__':
    main()