import serial
import time
import os
import io
from PIL import Image # <--- Przywrócono import
import numpy as np    # <--- Przywrócono import

# --- Konfiguracja ---
SERIAL_PORT = 'COM11' # Zmień na swój port
BAUD_RATE = 115200
START_MARKER = 'START_IMAGE_TRANSFER'
RESULTS_DIR = "tests_home_cam"
os.makedirs(RESULTS_DIR, exist_ok=True)

# Kolor tła użyty w kodzie C++ (Node A)
SPARSE_BACKGROUND_COLOR = 0 # 0x00 dla czarnego

# --- Przywrócono zmienne globalne ---
base_image_pil = None # Przechowuje ostatni pełny obraz (obiekt Pillow)
last_width = 0
last_height = 0
last_block_size = 0
# --------------------

def log(message):
    """Proste logowanie na konsolę."""
    timestamp = time.strftime("%H:%M:%S")
    print(f"[{timestamp}] {message}")

# --- Przywrócono funkcję apply_sparse_jpeg ---
def apply_sparse_jpeg(base_img_pil, sparse_jpeg_data, background_color=0):
    """
    Nakłada zmiany ze Sparse JPEG na obraz bazowy.

    Args:
        base_img_pil (PIL.Image.Image): Obraz bazowy (w skali szarości).
        sparse_jpeg_data (bytes): Surowe bajty odebranego Sparse JPEG.
        background_color (int): Wartość piksela tła użyta w Sparse JPEG.

    Returns:
        PIL.Image.Image: Zaktualizowany obraz bazowy lub None w przypadku błędu.
    """
    if base_img_pil is None:
        log("[!] Błąd: Obraz bazowy (base_image_pil) nie istnieje.")
        return None

    try:
        log("[*] Dekodowanie Sparse JPEG...")
        sparse_stream = io.BytesIO(sparse_jpeg_data)
        # Otwórz i upewnij się, że jest w skali szarości ('L')
        sparse_img_pil = Image.open(sparse_stream).convert('L')

        if sparse_img_pil.size != base_img_pil.size:
            log(f"[!] Błąd: Rozmiar Sparse JPEG ({sparse_img_pil.size}) różni się od bazowego ({base_img_pil.size})!")
            return None

        log("[*] Konwersja obrazów do NumPy dla szybkiego dostępu...")
        base_arr = np.array(base_img_pil, dtype=np.uint8)
        sparse_arr = np.array(sparse_img_pil, dtype=np.uint8)

        log(f"[*] Nakładanie zmian (ignorowanie pikseli o wartości {background_color})...")
        # Stwórz maskę, gdzie piksele w sparse NIE SĄ tłem
        changed_pixels_mask = sparse_arr != background_color

        # Użyj maski do skopiowania tylko zmienionych pikseli
        base_arr[changed_pixels_mask] = sparse_arr[changed_pixels_mask]

        log("[*] Konwersja zaktualizowanej tablicy z powrotem do obrazu Pillow...")
        updated_base_img_pil = Image.fromarray(base_arr, mode='L')
        log("[+] Obraz bazowy zaktualizowany.")
        return updated_base_img_pil

    except Exception as e:
        log(f"[!] Błąd podczas przetwarzania Sparse JPEG: {e}")
        return None
# --- Koniec funkcji apply_sparse_jpeg ---

def main():
    global base_image_pil, last_width, last_height, last_block_size # Używamy zmiennych globalnych

    log(f"Odbiornik obrazów (z obsługą Sparse JPEG). Czekam na {SERIAL_PORT}...") # Zmieniono log startowy

    try:
        with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2) as ser:
            log("Połączono! Oczekiwanie na dane...")
            image_counter = 0 # Licznik ogólny

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
                        filename_prefix = "" # Baza do nazw plików

                        # --- Przywrócono logikę przetwarzania obrazu ---
                        is_first_frame = (base_image_pil is None)

                        if is_first_frame:
                            # To musi być pierwszy, pełny obraz (frame_1)
                            log("[*] Przetwarzanie jako obraz bazowy (Full Frame)...")
                            try:
                                image_stream = io.BytesIO(image_data)
                                img_pil = Image.open(image_stream).convert('L')

                                if img_pil.size != (image_width, image_height):
                                     log(f"[!] Warning: Rozmiar zdekodowany ({img_pil.size}) różni się od nagłówka ({image_width}x{image_height})!")
                                     # Można dodać 'continue'

                                base_image_pil = img_pil # Zapisz jako bazę
                                last_width = image_width
                                last_height = image_height
                                last_block_size = block_size
                                filename_prefix = f"base_frame_MAD_uczelnia_16_10_{image_timestamp}"
                                log("[+] Obraz bazowy zapisany w pamięci.")

                            except Exception as e:
                                log(f"[!] Błąd dekodowania obrazu bazowego: {e}")
                                filename_prefix = f"error_base_frame_{image_timestamp}"
                                base_image_pil = None # Resetuj

                        else:
                            # To powinien być obraz różnicowy (Sparse JPEG)
                            log("[*] Przetwarzanie jako obraz różnicowy (Sparse JPEG)...")
                            filename_prefix = f"diff_frame_{image_timestamp}"

                            # Sprawdź, czy metadane pasują
                            if image_width != last_width or image_height != last_height:
                                log("[!] Ostrzeżenie: Wymiary diff różnią się od bazy! Traktuję jako nowy obraz bazowy.")
                                try:
                                    image_stream = io.BytesIO(image_data)
                                    base_image_pil = Image.open(image_stream).convert('L')
                                    last_width = image_width
                                    last_height = image_height
                                    last_block_size = block_size
                                    filename_prefix = f"new_base_frame_{image_timestamp}"
                                    log("[+] Nowy obraz bazowy zapisany.")
                                except Exception as e:
                                    log(f"[!] Błąd dekodowania nowego obrazu bazowego: {e}")
                                    filename_prefix = f"error_new_base_{image_timestamp}"
                                    base_image_pil = None
                            else:
                                # Wymiary pasują, nałóż zmiany
                                updated_image = apply_sparse_jpeg(base_image_pil, image_data, SPARSE_BACKGROUND_COLOR)
                                if updated_image:
                                    base_image_pil = updated_image # Aktualizuj bazę
                                    filename_prefix = f"updated_frame_MAD_uczelnia_16_10_{image_timestamp}"
                                else:
                                    log("[!] Nie udało się nałożyć zmian Sparse. Zapisuję tylko surowy plik diff.")
                                    filename_prefix = f"error_diff_frame_{image_timestamp}"
                        # --- Koniec logiki przetwarzania ---

                        # --- Zapisywanie plików (przywrócono) ---
                        # Zapisz SUROWE dane odebrane przez Serial
                        raw_filename = os.path.join(RESULTS_DIR, f"{filename_prefix}_raw.jpg")
                        try:
                            with open(raw_filename, 'wb') as f:
                                f.write(image_data)
                            log(f"[*] Surowe dane JPEG zapisane jako: {raw_filename}")
                        except Exception as e:
                            log(f"[!] Błąd zapisywania surowych danych: {e}")

                        # Jeśli mamy obraz w pamięci (bazowy lub zaktualizowany), zapisz go jako PNG
                        if base_image_pil:
                             processed_filename = os.path.join(RESULTS_DIR, f"{filename_prefix}_processed.png")
                             try:
                                 base_image_pil.save(processed_filename)
                                 log(f"[*] Przetworzony obraz (po nałożeniu zmian) zapisany jako: {processed_filename}")
                             except Exception as e:
                                 log(f"[!] Błąd zapisywania przetworzonego obrazu: {e}")
                        # --- Koniec zapisywania ---

                        log("\nOczekiwanie na kolejny obraz...")

                    elif line: # Loguj inne linie z Node C
                        log(f"[Node C]: {line}")

                except UnicodeDecodeError:
                     pass
                except serial.SerialTimeoutException:
                     log("[Timeout] Brak danych w oknie czasowym.")
                     pass
                except Exception as e:
                    log(f"[!] Błąd w pętli głównej: {e}")
                    time.sleep(1)

    except serial.SerialException as e:
        log(f"BŁĄD KRYTYCZNY: Nie można otworzyć portu {SERIAL_PORT}. {e}")
    except KeyboardInterrupt:
        log("\nProgram zatrzymany przez użytkownika.")

if __name__ == '__main__':
    main()